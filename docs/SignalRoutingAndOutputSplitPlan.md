# Plano de implementação — split de linha, split de sinal e split de output

Status: proposta de implementação (ainda não é código).
Data: 2026-11-08.

## 1. Objetivo

Transformar o roteamento do OpenGuitarMultiFx de "linhas ligadas por booleanos" em um modelo de **rotas explícitas**, cobrindo três casos que hoje estão misturados ou ausentes:

1. **Split de linha** — uma linha alimenta várias linhas e/ou saídas físicas ao mesmo tempo.
2. **Split de sinal** — controle de canal/mono/estéreo por rota (gain, pan, fase, channel map; crossover fica para uma fase posterior).
3. **Split de output** — uma linha pode mandar para mais de um par de saída físico ao mesmo tempo, e linhas diferentes podem sair por pares diferentes simultaneamente.

Referências de mercado que o desenho deve buscar: blocos explícitos de split/merge/mixer (Line 6 Helix, Neural DSP Quad Cortex), paths paralelos com level/pan/balance/mute/solo, e tratamento formal de mono/estéreo por bloco/caminho.

## 2. Estado atual (o que já existe e o que falta)

### O que já funciona

- `SignalGraph` já tem lanes paralelas reais, split e merge, com soma no merge e processamento paralelo por estágio (`Source/Engine/SignalGraph.h`, `Source/Engine/SignalGraph.cpp`).
- `MainComponent::RowRouting` já modela, por linha, entrada, saída para device e máscara de linhas destino (`Source/UI/MainComponent.h`).
- Presets já persistem o roteamento por linha em `<Routing><Row .../></Routing>` (`Source/UI/MainComponent.cpp`).
- Bypass com tails, spillover de preset e swap fade de grafo já existem e devem ser preservados.

### Limitações que este plano ataca

1. **Output split não existe.** Cada linha tem no máximo um `toDevice` + um `deviceOutputPair`; o menu trata a escolha como "mover", não "adicionar" (`Source/UI/MainComponent.cpp`). `AudioEngine` tem um único `selectedOutputPairStart` atômico e, no fim do callback, silencia todos os canais fora desse par (`Source/Engine/AudioEngine.cpp`). Resultado: duas linhas não podem sair por pares diferentes ao mesmo tempo, e uma linha não pode sair por dois pares.
2. **Rotas linha→linha são somas puras.** Não há gain, pan, fase, dry/wet ou channel map por rota; `addLaneConnection()` só recebe `(sourceLane, targetLane)` e o merge usa `addClamped()` sem peso.
3. **Modelo de dados mistura topologia e distribuição.** `RowRouting` mistura "de onde a linha lê", "para onde ela escreve" e "quais linhas ela alimenta" em booleanos/máscaras. Isso dificulta expressar rotas ricas sem quebrar presets.
4. **Entrada ainda é global na prática.** O engine abre/seleciona um único canal de entrada; várias linhas podem ler esse mesmo input, mas `applyPresetXml()` chama `setInputChannel()` uma vez por linha alimentada, então a última linha vence. O plano trata isso como bug latente a corrigir na migração.
5. **Mono/estéreo não é cidadão de primeira classe.** O grafo processa buffers estéreo, mas não há primitive de `Mono`, `Split L/R`, `Merge L/R`, `Duplicate/Sum` nem crossover.

## 3. Princípios

- **Não quebrar presets antigos.** `<Routing>` atual e o fallback `<IO>` continuam carregando; a escrita nova usa schema versionado.
- **Não mexer no contrato dos efeitos.** `EffectProcessor` continua processando um `juce::AudioBuffer<float>`; roteamento/channel map ficam fora dos processadores.
- **Realtime boundary intacta.** Nenhuma alocação, mutex, I/O ou espera no callback. Tabelas de rota são montadas na control thread e publicadas por `DeferredReclaimer` ou estrutura imutável pré-alocada.
- **Som antes de CPU.** Qualquer otimização de roteamento que mude o sinal renderizado só entra se passar por um gate de áudio mensurável; do contrário, documentar números e não mergear.
- **Sem caso especial por efeito.** `SignalGraph` e o roteador não conhecem tipos concretos de processador.

## 4. Modelo alvo

### 4.1 Tipos compartilhados

Criar `Source/Engine/SignalRoute.h` com tipos POD reutilizáveis por engine, UI e presets:

```cpp
enum class DestinationKind { lane, deviceOutputPair };

enum class ChannelMap
{
    stereo,      // L -> L, R -> R
    monoLeft,    // L -> L+R
    monoRight,   // R -> L+R
    monoSum,     // (L + R) * 0.5 -> L+R
    swapLR       // L -> R, R -> L
};

struct RouteSpec
{
    DestinationKind kind = DestinationKind::lane;
    int destination = -1;      // lane index ou output pair index
    float gain = 1.0f;         // linear; UI/preset pode falar em dB
    float pan = 0.0f;          // -1..1, aplicado depois do channel map
    bool invertPhase = false;
    ChannelMap channelMap = ChannelMap::stereo;
};
```

Limites fixos para a thread de áudio (sem `std::vector` no caminho realtime):

- `maxLanes = 4` (igual a `MainComponent::numRows` hoje).
- `maxLaneRoutesPerLane = 8`.
- `maxOutputRoutes = 32`.
- Pares de saída reais vêm de `AudioEngine::getAvailableOutputPairNames()`; o teto atual de canais sãos é 16, ou seja, até 8 pares.

A UI pode usar `std::vector<RouteSpec>` durante a edição; `MainComponent::rebuildSignalGraph()` compila isso em tabelas fixas antes de publicar.

### 4.2 Grafo de lanes com rotas ponderadas

`SignalGraph` continua com 4 lanes e ordem fixa 0→3 (a regra forward-only da UI já garante ordem topológica). O que muda:

- Conexões deixam de ser `pair<int,int>` e viram `LaneRoute { sourceLane, targetLane, gain, invertPhase, channelMap }`.
- `addLaneConnection(source, target)` permanece como wrapper de compatibilidade com `gain = 1`, `invertPhase = false`, `channelMap = stereo` — os testes atuais continuam válidos.
- Novo `addLaneRoute(const LaneRoute&)` para rotas ricas.
- No merge (`runLane`), cada fonte é transformada pelo `ChannelMap`/ganho/fase da rota antes de somar no buffer da lane destino. Como não se pode alocar por bloco, `prepare()` pré-aloca um scratch estéreo por lane (ou por aresta ativa) e o áudio reutiliza.
- A ordem de processamento por estágios/worker threads se mantém; rotas ponderadas não mudam a topologia, só o peso da soma.

### 4.3 OutputMatrix: roteamento lane → pares físicos

Hoje `AudioEngine` mistura todas as lanes com `writesDeviceOutput` em um único buffer estéreo e depois silencia tudo fora de um par global. A evolução proposta:

- Novo `Source/Engine/OutputMatrix.{h,cpp}`: objeto imutável, preparado na control thread, que recebe:
  - os buffers de saída das lanes do `SignalGraph` ativo;
  - o buffer de saída do dispositivo (todos os canais abertos);
  - uma tabela fixa de `OutputRoute { sourceLane, outputPairIndex, gain, pan, invertPhase, channelMap }`.
- `OutputMatrix::render()`:
  1. limpa todos os canais de saída do dispositivo;
  2. para cada rota válida, lê a lane fonte, aplica channel map/ganho/pan/fase e soma nos dois canais do par destino;
  3. rotas múltiplas para o mesmo par somam (merge de output);
  4. canais sem rota ficam mudos.
- `AudioEngine` passa a publicar uma `DeferredReclaimer<OutputMatrix>` ao lado do `DeferredReclaimer<SignalGraph>`. O callback chama `graph->processLanes()` (novo método que só computa lanes) e depois `outputMatrix->render(...)`.
- `SignalGraph::process(buffer)` legado permanece como wrapper para testes e spillover durante a transição: roda lanes e mistura lanes com `writesDeviceOutput` como hoje. O caminho principal do `AudioEngine` migra para `processLanes()` + `OutputMatrix`.

Fallback de compatibilidade: se nenhuma `OutputMatrix` estiver publicada (ou ela estiver vazia), o callback mantém o comportamento atual — passthrough seco no par global — para não silenciar o app em presets antigos ou estado inicial.

### 4.4 Onde master volume/EQ entra

Manter a semântica atual: master gain/EQ é pós-grafo e pré-medidor de saída. Com `OutputMatrix`, a ordem proposta é:

```text
input -> SignalGraph lanes -> OutputMatrix (roteamento físico) -> MasterEq -> masterGain -> OUT meter
```

Isso preserva "o que o usuário ouve no OUT é o que foi medido". Trim por par de saída, se um dia for necessário, entra como fase posterior dentro da própria rota (não como segundo master).

## 5. Migração de presets

### 5.1 Leitura (compatível)

`MainComponent::applyPresetXml()` passa a ter dois caminhos:

- **Formato novo:** `<Routing version="2">` com elementos por rota.
- **Formato antigo:** `<Routing>` sem `version` ou com `<Row .../>` atributos atuais. Cada `<Row>` vira:
  - `inputChannel` → estado de entrada da linha (ver correção do bug de "última linha vence" abaixo);
  - cada bit de `toRowsMask` → `RouteSpec { kind = lane, destination = target, gain = 1, pan = 0, invert = false, channelMap = stereo }`;
  - `toDevice = true` → `RouteSpec { kind = deviceOutputPair, destination = deviceOutputPair / 2, ... }`.
- `<IO>` antigo continua mapeando para linha 0 device-in → device-out.

### 5.2 Escrita (novo)

Exemplo de schema v2:

```xml
<Routing version="2">
  <Lane index="0" inputChannel="0">
    <Output kind="lane" target="1" gainDb="0" pan="0" invert="0" channelMap="stereo"/>
    <Output kind="lane" target="2" gainDb="-6" pan="0" invert="1" channelMap="monoSum"/>
    <Output kind="device" pair="0" gainDb="0" pan="0" invert="0" channelMap="stereo"/>
    <Output kind="device" pair="2" gainDb="-3" pan="0.2" invert="0" channelMap="stereo"/>
  </Lane>
</Routing>
```

Regras:

- `pair` é o índice do par retornado por `getAvailableOutputPairNames()` (0, 1, 2...). O valor legado `deviceOutputPair` é canal inicial (0, 2, 4...); na migração, `pair = deviceOutputPair / 2`.
- `gainDb` é conveniência de UI/preset; o engine guarda ganho linear.
- Atributos ausentes assumem defaults (`gainDb=0`, `pan=0`, `invert=0`, `channelMap="stereo"`), então um XML v2 mínimo continua legível.

### 5.3 Correção do bug de entrada

Como o engine hoje tem um único canal de entrada selecionado, o modelo v2 deve escolher **uma** entrada global de forma determinística:

- se várias linhas têm `inputChannel >= 0`, usar a primeira linha alimentada (ou um `<Input channel="..."/>` explícito no schema v2);
- nunca chamar `setInputChannel()` em loop deixando a última vencer;
- documentar que múltiplas entradas físicas distintas por linha ficam para uma fase futura (exigiria abrir/selecionar vários canais de entrada no device).

## 6. Impacto por arquivo

| Arquivo | Mudança |
|---|---|
| `Source/Engine/SignalRoute.h` (novo) | `DestinationKind`, `ChannelMap`, `RouteSpec`, limites fixos, helpers de ganho/pan/channel map. |
| `Source/Engine/LaneRoute.h` (novo ou dentro de `SignalRoute.h`) | Rota ponderada lane→lane. |
| `Source/Engine/SignalGraph.h/.cpp` | Conexões ponderadas, `addLaneRoute()`, `processLanes()`, accessor const dos buffers de lane, scratch de merge pré-alocado em `prepare()`. |
| `Source/Engine/OutputMatrix.h/.cpp` (novo) | Tabela fixa de rotas lane→par físico + `render()` realtime-safe. |
| `Source/Engine/AudioEngine.h/.cpp` | `DeferredReclaimer<OutputMatrix>`, callback usa `processLanes()` + `render()`, fallback legado, OUT meter lê o buffer final. `setOutputChannelPair()`/`getOutputChannelPair()` viram legado até a UI migrar por completo. |
| `Source/UI/MainComponent.h/.cpp` | `RowRouting` vira wrapper sobre `std::vector<RouteSpec>` (ou é substituído), menus de output aceitam múltiplos destinos, preset save/load v2 + migração. |
| `Source/UI/RowEndpointBlock.h/.cpp` | Tile de output mostra múltiplos destinos; long-press/edição abre card de rota (gain/pan/invert/channel map) via `OverlayHost`, sem dialog de OS. |
| `Tests/SignalGraphTests.cpp` | Novos testes de rota ponderada; testes atuais de split/merge continuam via wrapper legado. |
| `Tests/OutputMatrixTests.cpp` (novo) | Renderização com lanes sintéticas: múltiplos pares, soma no mesmo par, canais inativos mudos, channel maps, fallback. |
| Testes de preset | Roundtrip v2 e carga de preset antigo com roteamento equivalente. |
| `Source/Engine/AGENTS.md`, `Source/UI/AGENTS.md`, `ARCHITECTURE.md` | Atualizar decisões/pitfalls quando o código entrar. |

## 7. Fases de entrega

### Fase 0 — preparar sem mudar som

- Adicionar testes que capturem o comportamento atual de split/merge e de output único.
- Corrigir a seleção determinística de canal de entrada no load de preset (bug latente), sem mudar o schema ainda.
- Rodar `scripts/test.sh` e benchmark de callback para ter baseline.

### Fase 1 — OutputMatrix com o modelo atual (PR recomendado)

Escopo: **split de output entre linhas diferentes**, ainda sem múltiplos outputs por linha e sem gain/pan.

- Criar `OutputMatrix` e `OutputRoute` com `gain = 1`, `pan = 0`, `invert = false`, `channelMap = stereo`.
- `MainComponent::rebuildSignalGraph()` continua lendo `RowRouting` antigo, mas também monta a tabela de rotas de device a partir dele (no máximo um par por linha, como hoje).
- `AudioEngine` passa a rotear cada lane para o par configurado naquela linha, em vez de um par global único.
- Ganho real: duas linhas podem sair por pares diferentes ao mesmo tempo; uma linha continua com um par só.
- Presets antigos carregam sem mudança de schema.

Critério de pronto: testes de `OutputMatrix` verdes; preset antigo soa igual; duas linhas em pares diferentes são audíveis simultaneamente.

### Fase 2 — RouteSpec e split de output por linha

- Substituir `toDevice`/`deviceOutputPair` por lista de rotas de device por linha.
- UI: menu de output vira multi-toggle de pares + linhas; cada destino pode ser removido individualmente.
- Preset v2 write/read com migração do formato antigo.
- `RowEndpointBlock` mostra "Out 1/2 + Out 3/4" ou "Line 2 + Out 1/2" de forma legível.

### Fase 3 — rotas ponderadas (gain/pan/fase/channel map)

- `LaneRoute` ponderada no `SignalGraph`.
- `OutputRoute` com gain/pan/invert/channel map.
- Card de edição de rota por destino (toque): gain (dB), pan, invert, channel map.
- Testes de soma com ganho, inversão de fase e `monoSum`/`monoLeft`/`monoRight`/`swapLR`.

### Fase 4 — split de sinal formal

- Primitivas de canal explícitas na UI/engine: `Mono`, `Stereo`, `Split L/R`, `Merge L/R`, `Duplicate Mono to Stereo`, `Sum to Mono`.
- Avaliar crossover (HP/LP) como bloco de split com filtro; só depois de channel map estar estável.
- Revisar se algum `EffectProcessor` precisa declarar preferência de canal (ex.: amp mono vs reverb estéreo) — sem acoplar `SignalGraph` a tipos concretos.

### Fase 5 — spillover com roteamento

- Hoje spillover soma no bus principal. Evoluir para que a cauda de um preset saia pelas mesmas rotas de output que a linha tinha, ou documentar explicitamente que spill segue o bus principal.

## 8. Testes e gates

- **Unitários de grafo:** manter split/merge atuais; adicionar gain=0.5, invert, channel map e soma com clipping controlado.
- **Unitários de OutputMatrix:** lanes sintéticas → canais de device; múltiplas rotas para o mesmo par somam; par sem rota fica mudo; fallback legado quando não há matriz.
- **Presets:** preset antigo com split/merge carrega e soa igual; preset v2 faz roundtrip; `<IO>` continua funcionando.
- **Benchmark:** `SignalGraphBench`/`CallbackOverheadBench` antes/depois; o custo extra deve ser proporcional ao número de rotas, não ao número de canais do device.
- **Gate de áudio:** antes de mergear qualquer PR que mude sinal renderizado, renderizar a amostra DI do repo através de um preset com split/merge e output split, conferir que sai som e que se parece com a fonte; anexar o render.

## 9. Riscos e decisões em aberto

1. **Pan em buffer estéreo:** definir lei de pan (constant-power vs balance) e se pan faz sentido antes de channel map. Proposta: aplicar channel map primeiro, depois pan como balance L/R.
2. **Clipping no merge:** hoje `addClamped()` soma sem clamp real. Rotas com ganho devem deixar o usuário controlar nível; o master EQ/gain continua como trim final. Não introduzir limiter escondido no roteador.
3. **Spillover:** decidir se caudas seguem as rotas antigas (mais correto, mais complexo) ou o bus principal (mais simples). Proposta: fase 5.
4. **Múltiplas entradas físicas:** o engine ainda seleciona um canal de entrada global. Separar input por linha de verdade exige mudança no `AudioEngine`/device setup; fica fora das fases 1–3.
5. **UI touch:** card de rota não pode virar dialog de OS; usar `OverlayHost` e respeitar `touch::minTapTarget`.

## 10. Recomendação de primeiro passo

Começar pela **Fase 1 (OutputMatrix com o modelo atual)** porque:

- já resolve a limitação mais sentida de output (linhas em pares diferentes ao mesmo tempo);
- não exige mudar o schema de preset ainda;
- cria a estrutura (`OutputRoute`, publicação via `DeferredReclaimer`, render no callback) que as fases 2–4 só vão enriquecer;
- tem teste unitário isolado possível sem device de áudio real.

Depois, a **Fase 2** entrega o "split de output" que você pediu de forma visível na UI, e a **Fase 3** entrega o "split de sinal" com ganho/fase/canal.
