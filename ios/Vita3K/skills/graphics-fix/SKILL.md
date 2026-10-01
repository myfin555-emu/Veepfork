---
name: graphics-fix
description: Investigar e corrigir glitches visuais de jogos no Vita3K iOS com modo gráfico de logs persistentes, reprodução guiada, screenshots e análise de shaders e renderização.
---

Use no checkout Vita3K com `ios/Vita3K/scripts/diagnostics.py`. Leia
`ios/Vita3K/DIAGNOSTICS.md` para o protocolo, coleta visual e limites dos hashes.

## Armar e mostrar o defeito

1. Se o usuário já reproduziu em caso pendente, retome
   `build/diagnostics/<caso>/case.json`; não reinicie nem reinstale antes de
   coletar logs e a tela ainda visível.
2. Identifique o aparelho, jogo e cena. Aproveite informações existentes; peça
   detalhes visuais ausentes enquanto prepara a coleta. Não substitua iPhone
   por simulador como comprovação de correção gráfica no aparelho.
3. Confirme build com diagnósticos. Se preciso, compile/atualize segundo
   `ios/Vita3K/README.md`, preserve o container e guarde binário/dSYM/UUIDs.
4. Execute `python3 ios/Vita3K/scripts/diagnostics.py arm --mode graphics --device ID`
   (ou `--simulator UDID`). Guarde CASE na conversa. Preserve evidências da
   execução anterior antes de reiniciar; JIT pode precisar de reativação.
5. Depois de abrir o app, `status --case CASE` deve confirmar ACK novo no modo
   **graphics**. Se depender do usuário para abrir, aguarde e verifique antes
   de pedir a reprodução. Então peça para jogar até o defeito, manter a cena
   visível e confirmar, descrevendo o que deveria aparecer. Screenshot/vídeo
   do usuário pode ser pedido em texto normal quando a captura direta falhar.
   Aguarde a reprodução; ausência de erro no log não prova imagem correta.

## Analisar e corrigir

1. Com a cena visível, `collect --case CASE --screenshot`. Inspecione a imagem
   capturada e correlacione horário/title ID com runtime, telemetry e config.
   Confira `collection-errors.json`; não afirme ter screenshot se a captura
   falhou. Preserve também uma referência correta fornecida pelo usuário.
2. Analise hashes de shaders/pipelines, draw samples, pipeline_ready, resolução,
   surface sync e mapping. Amostras esparsas não cobrem todos os draws nem
   contêm buffers completos. Diferencie compilação assíncrona temporária,
   textura/formato/swizzle, sync de superfície, depth/stencil/blending,
   tradução USSE/SPIR-V e apresentação via MoltenVK.
3. Se faltarem dados, instrumente o caminho suspeito ou prepare uma captura
   Metal GPU no Xcode. Dumps completos de shaders/texturas/uniforms devem
   limitar-se a uma reprodução curta. Preserve caches antes de qualquer teste
   que os invalide, e compare cache frio/quente separadamente.
4. Corrija a causa apoiada por evidência. Se indispensável um workaround por
   jogo, use title ID exato/revisão, opção default desligada e custom config
   por título; nunca uma alteração global baseada em nome parcial.
5. Compile e rode checks relevantes; compare screenshots antes/depois na
   mesma cena, resolução e configuração. Verifique outra cena/título afetado
   pelo caminho compartilhado. Reteste no aparelho quando a correção depende
   do driver/MoltenVK. Build bem-sucedido não comprova correção visual.
6. Registre evidência, hipótese, alteração e validação em `CASE/analysis.md`.
   Depois de coletar, `disarm --case CASE` volta ao normal no próximo boot;
   uma nova captura usa um novo caso. Relate pendências sem prometer correção
   se não foi possível reproduzir/validar a imagem.
