---
name: compat-fix
description: Investigar e corrigir crashes, freezes e stucks de jogos no Vita3K iOS, armando captura persistente, guiando a reprodução e coletando os logs após a confirmação do usuário.
---

Use no checkout Vita3K com `ios/Vita3K/scripts/diagnostics.py`. Leia
`ios/Vita3K/DIAGNOSTICS.md` para o protocolo, limites e interpretação dos dados.
Se não estiver nesse checkout, localize-o antes de executar comandos.

## Armar e reproduzir

1. Se já existe um caso pendente e o usuário diz que crashou/travou, retome
   `build/diagnostics/<caso>/case.json` e vá direto à coleta. Não reinicie o app,
   não rearme e não reinstale antes de preservar a evidência.
2. Identifique o aparelho alvo conectado e o jogo/title ID quando informado.
   Havendo ambiguidade real, pergunte o alvo. Não substitua uma reprodução de
   iPhone por simulador sem explicitar que o comportamento pode ser diferente.
3. Verifique se o build tem os novos diagnósticos. Quando necessário, compile
   core/app seguindo `ios/Vita3K/README.md`, preserve os dados do container ao
   atualizar e guarde binário/dSYM/UUIDs correspondentes no caso. Não use a
   revisão do checkout como prova da versão instalada.
4. Execute `python3 ios/Vita3K/scripts/diagnostics.py arm --mode compat --device ID`
   (ou `--simulator UDID`). Guarde o caminho CASE retornado na conversa.
   O marcador deve estar copiado antes da próxima inicialização. Se o app já
   estiver rodando, preserve os logs existentes antes de reiniciá-lo. A abertura
   manual pode exigir reativar JIT; não prometa que o helper faz isso.
5. Após abrir o app, execute `status --case CASE` e confirme modo **compat** e
   uma captura nova. Só então peça: “Jogue até o crash ou travamento e me avise
   quando ocorrer; diga a cena e se o app fechou ou a imagem ficou parada.”
   Se depender da abertura manual, primeiro peça para abrir, aguarde essa
   confirmação e verifique o ACK antes da reprodução. Encerre a etapa aguardando
   a ação humana; não declare que houve crash por timeout ou falta de frames.

## Depois da confirmação

1. Execute `collect --case CASE` antes de relançar, encerrar ou atualizar. Para
   crash, procure o `.ips` relevante em `systemCrashLogs` e recolha com
   `collect --case CASE --crash-file NOME.ips`; use a alternativa Xcode descrita
   no guia se o serviço falhar. Para stuck, tente capturar stacks enquanto vivo.
   Não colete indiscriminadamente relatórios de outros apps.
2. Leia manifest, runtime, telemetry, configurações e erros de coleta. Use
   `analyze DIRETORIO_DA_COLETA/diagnostics` como índice. Correlacione jogo,
   horário, último progresso guest/host, fase do renderer, PC/LR/NID e threads.
   Breadcrumb não é stack nem prova de deadlock. Diferencie crash nativo,
   jetsam, falha guest, espera legítima, JIT revogado e loop guest/HLE.
3. Registre hipótese e evidência em `CASE/analysis.md`, incluindo lacunas. Se
   necessário, adicione instrumentação pontual no código suspeito e peça nova
   reprodução, sem habilitar indiscriminadamente trace de instruções/imports.
4. Corrija quando houver causa sustentada. Execute os checks apropriados,
   compile, valide o caminho afetado e peça reteste da mesma cena no aparelho.
   Não trate uma compilação bem-sucedida como crash resolvido.
5. Após preservar os dados, `disarm --case CASE` prepara o próximo boot normal.
   Se ainda precisar de nova captura, rearme explicitamente em um novo caso.
   Termine com causa/evidência, alteração, validação e pendências reais.
