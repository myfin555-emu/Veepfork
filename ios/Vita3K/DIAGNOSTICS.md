# Diagnóstico de jogos no iOS

`Documents/diagnostics-mode.txt` aceita `compat`, `performance`, `graphics` ou
`off`. É lido na inicialização do core e permanece válido nas próximas aberturas
até ser desativado. Não muda configurações salvas. Cada processo cria
`Documents/diagnostics/capture-<unix-us>/`. Os jogos recebem `game` incremental e
`title_id` nos eventos. `diagnostics/active.txt` confirma modo e captura ativos.

## Logging normal e custo de execução

No iOS, os logs ficam **desativados por padrão**, inclusive em instalações
antigas cujo `config.yml` tinha `log-level: 0` (Trace). Para habilitar: manter
pressionado o jogo → **Editar Config** → **Logs (este jogo)** → **Log Level**.
`Desativado` é o nível 6; Trace a Critical continuam sendo 0 a 5. A escolha e
os avisos de compatibilidade ficam no XML desse título (`ios-log-level` e
`ios-log-compat-warn`), entram em vigor no próximo início do jogo e não afetam
os outros títulos. Remover a configuração própria restaura logs desativados.

Os três modos de diagnóstico têm prioridade sobre essa escolha durante todo o
processo: Compat/Graphics usam Debug, Performance usa Info, inclusive se o
jogo tiver Trace salvo. Desarmar o modo e reabrir o app restaura a política
normal. Nenhum modo altera os valores salvos do jogo.

Chamadas `LOG_*` filtradas não avaliam os argumentos no iOS. `LOG_*_ONCE`
também não altera sua flag atômica até que o nível permita a mensagem. Os
probes Swift automáticos seguem a mesma ativação e não interpolam strings nem
escrevem arquivos quando desativados. O polling da interface só pede o
relatório completo de sessão em caso de falha; controles, IME e verificação
de JIT continuam ativos. Testes manuais e erros apresentados na interface
continuam disponíveis. O logger mantém sua infraestrutura inicializada e
pode criar um arquivo vazio; não há escrita de mensagens enquanto está Off.
Os dumps de diagnóstico de shaders (GXP/disassembly) também não são gravados
com logs Off. O código gerado e a leitura/gravação do cache de shaders são
preservados; a conversão de shaders permanece a mesma.
O nível próprio do MoltenVK acompanha os logs do core (incluindo Off).
Durante uma captura, o callback Vulkan preserva os erros do driver mesmo sem
camadas de validação instaladas. Erros recebem eventos síncronos `vulkan_error`
(até 8 KiB por mensagem). Falhas ao criar um pipeline recebem `pipeline_failure`
com hashes dos shaders e estados de vértices/blend/depth, inclusive para shaders
vindos do cache. A instrumentação mantém o comportamento da exceção original.
Seu modo debug e as camadas de validação Vulkan, quando disponíveis e
habilitadas na configuração, só são usados em Debug/Trace ou Compat/Graphics;
a captura Performance permanece em Info sem essa instrumentação adicional.

Tracy já usa `TRACY_ON_DEMAND`; as métricas de diagnóstico já verificam se há
captura ativa antes de medir tempos. Não foram alterados CPU/JIT, sincronização
de superfícies, shaders, caches ou precisão de renderização. Esta redução de
trabalho de logging não constitui uma medição de ganho de FPS.

## Avaliação dos logs existentes

- `vita3k/util/src/logging.cpp` truncava `vita3k.log` ao iniciar. A fila assíncrona
  usa `overrun_oldest`: mensagens podem desaparecer antes de chegar ao disco.
  TRACE não resolve isso nem produz um perfil de tempos.
- `LOG_ONCE` e o filtro de duplicatas ocultam recorrências. Ausência de linhas
  não prova ausência de erros. O handler de exceção do logger só cobre Windows.
- Tracy tem zonas de HLE/render, mas `TRACY_ON_DEMAND` exige cliente conectado.
  Não equivale a um arquivo persistente pós-crash. `TRACY_NO_CRASH_HANDLER`
  evita interferir nos faults usados pelo emulador.
- O relatório Swift depende da interface. A nova coleta usa uma thread própria,
  try-locks e breadcrumbs publicados pelo dono do JIT; não lê registradores de
  uma CPU guest em execução a partir de outra thread.

## Dados coletados

| Modo | Coleta adicional |
| --- | --- |
| compat | DEBUG, avisos de compatibilidade, até 64 threads: estado, último PC/LR/NID publicado e progresso |
| performance | INFO, histogramas dos intervalos de frames guest/host e tempos de batches/render/present/pipelines; sem trace por instrução/import |
| graphics | DEBUG, hashes de até 256 pipelines por processo e amostra de draw a cada 100 ms: shaders, primitive, quantidade, resolução e pipeline disponível |

Todos registram fases, título/versão, configuração efetiva, GPU/mapping, modelo,
estado inicial do JIT, memória física, CPU acumulada, temperatura, Low Power Mode
e contador de mensagens sobrescritas na fila. Amostras agregadas a cada segundo;
eventos de fase imediatos. Relógio monotônico nos eventos, epoch no manifest
para correlação com runtime log e `.ips`.

A biblioteca indica o modo ativo em uma faixa fixa no topo: **Compat Fix**,
**Performance Fix** ou **Graphics Fix**. Sem captura específica, **Debug**
aparece quando o nível efetivo de logs é Debug/Trace; níveis Info ou superiores
não mostram a faixa. Esse estado é independente do indicador de JIT. A faixa
lê o estado do processo atual: armar/desarmar um marcador só muda o modo de
captura após reabrir o app. A faixa acompanha o nível efetivo em até um segundo;
editar a preferência de um jogo não ativa logs na biblioteca.

- `runtime.log`, `runtime.1.log`, `runtime.2.log`: até 8 MiB cada; assíncrono.
- `telemetry.jsonl`, `telemetry.1.jsonl`, `telemetry.2.jsonl`: até 2 MiB cada;
  logger separado com flush por linha. Não há I/O no lock usado pelas métricas.
- `manifest.txt`: formato, modo, versão, limites. A coleta também copia `config`.
- `context-N.jsonl`: identificação e configurações de cada jogo, preservadas
  mesmo quando uma sessão longa rotaciona as primeiras linhas de telemetry.
- `logs/vita3k.log` roda em arquivos limitados enquanto o modo está ativo.
  Cada captura usa até aproximadamente 30 MiB, além do log principal.
  Capturas antigas são preservadas, sem exclusão automática: depois de arquivar
  e verificar os arquivos no Mac, remover as antigas pelo app Arquivos.

Não há novo signal handler: SIGSEGV/SIGBUS fazem parte do tratamento de memória
do JIT. Flush reduz a perda, mas não garante a última mensagem após SIGKILL,
jetsam ou falha da thread de logging. Coletar também o `.ips` do sistema.

Em `health.detail`, `renderer_stage` é 0=parado, 1=batches/esperas, 2=render,
3=present. Idade de frame -1 significa nenhum frame observado. Breadcrumb stage:
1=JIT, 2=import/HLE, 3=parked, 4=retorno. Thread status: 0=run, 1=dormant,
2=suspend, 3=wait, -1=lock ocupado. PC/LR/NID são últimas observações, não um
backtrace nem um snapshot atômico de todos os campos. PC repetido pode indicar
loop legítimo; confirmar com a cena e progresso das outras threads.
`thermal`: 0=nominal, 1=fair, 2=serious, 3=critical.

Frame metrics contam **intervalos completos**, sem o primeiro frame. Host pode
reapresentar a mesma imagem: não confundir host FPS com FPS guest. Tempos são
wall time da CPU, incluindo esperas, não tempo de GPU. Pipeline inclui tradução
e driver. Percentis são limites superiores dos histogramas. Separar gameplay
de pausas/background/loading/warm-up ao comparar resultados.

## Operação

Da raiz do repositório, com alvo explícito:

```sh
python3 ios/Vita3K/scripts/diagnostics.py arm --mode compat --device DEVICE_ID
# Alternativas: --simulator SIMULATOR_UDID ou --documents /caminho/Documents
# Guardar o CASE impresso e retomar esse mesmo caso depois da reprodução.
python3 ios/Vita3K/scripts/diagnostics.py status --case CASE
python3 ios/Vita3K/scripts/diagnostics.py collect --case CASE
python3 ios/Vita3K/scripts/diagnostics.py analyze CASE/collection-TIMESTAMP/diagnostics
python3 ios/Vita3K/scripts/diagnostics.py disarm --case CASE
```

Após `arm`, abrir novamente Vita3K e habilitar JIT se necessário. Antes de pedir
a reprodução, conferir com `status` um ACK **novo**, no modo correto. Copiar o
marcador não ativa um processo já aberto. Se a abertura for manual, pedir que
o usuário abra o app, verificar o ACK, então orientar a jogar. `case.json` em
`build/diagnostics/` permite retomar outra conversa. Revisão do checkout não
prova o binário instalado: guardar app/dylib/dSYM usados, hashes/UUIDs
(`dwarfdump --uuid`) e diff no caso.

O helper nunca inicia, encerra ou reinstala o app nem limpa caches. Após o
problema, coletar antes de relançar/atualizar. Cada coleta cria uma pasta nova,
SHA-256 e relatório de arquivos ausentes. Fonte ativa pode mudar durante cópia;
uma segunda coleta após saída normal complementa a primeira evidência do stuck.
Não copia jogos, firmware, saves ou chaves. Revisar caminhos locais nas configs
antes de compartilhar o pacote fora da máquina.

Para glitches: `collect --screenshot` com a cena visível. Para crash físico,
listar arquivos e selecionar só o Vita3K/jetsam correspondente ao horário:

```sh
xcrun devicectl device info files --device DEVICE_ID --domain-type systemCrashLogs
python3 ios/Vita3K/scripts/diagnostics.py collect --case CASE --crash-file NOME.ips
```

No simulador, procurar `~/Library/Logs/DiagnosticReports/`. Alternativas:
Xcode → Devices and Simulators → View Device Logs, ou Dados de Análise do iOS.
Manter dSYM/binário correspondentes para simbolicação. Stuck pode exigir stacks
via Instruments/LLDB e nunca gerar `.ips`; coletar antes de parar o processo.

Hashes não substituem screenshot/Metal GPU Capture. Para atribuição CPU/GPU,
usar Instruments Game Performance/Time Profiler/Metal System Trace ou Tracy
conectado. Dumps completos de shader/textura/uniform só em reprodução pontual:
`log_active_shaders` imprime buffers por draw e perturba benchmarks.

## Skills e hacks

Fontes: `ios/Vita3K/skills/`. Instalar cada pasta em `~/.codex/skills/` ou
`$CODEX_HOME/skills` para invocar `$compat-fix`, `$performance-fix`, `$graphics-fix`.
As skills armam, pedem reprodução, retomam o caso após confirmação, coletam e
analisam antes de corrigir. Desarmar após coletar para voltar ao uso normal.

Hacks exigem evidência reproduzível. Usar custom config em
`vita3k/config/src/settings.cpp`, opção default desligada, title ID/revisão
exatos e possibilidade de desativar. Não casar por nome parcial nem mudar
defaults globais. Medir A/B na mesma cena/config/temperatura e conferir título
sem a opção. `fps_hack` muda pacing do guest; não é otimização genérica.

Referências: [spdlog async](https://github.com/gabime/spdlog/wiki/Asynchronous-logging),
[flush](https://github.com/gabime/spdlog/wiki/Flush-policy),
[crash reports Apple](https://developer.apple.com/documentation/xcode/diagnosing-issues-using-crash-reports-and-device-logs),
[performance Metal](https://developer.apple.com/documentation/xcode/analyzing-the-performance-of-your-metal-app/).
