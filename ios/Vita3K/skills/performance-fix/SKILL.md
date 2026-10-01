---
name: performance-fix
description: Investigar slowdown, stutter e baixa performance de jogos no Vita3K iOS com captura de métricas, reprodução guiada, análise e correções ou hacks restritos ao jogo afetado.
---

Use no checkout Vita3K que contém `ios/Vita3K/scripts/diagnostics.py`. Leia
`ios/Vita3K/DIAGNOSTICS.md` para operar o protocolo e interpretar métricas.

## Preparar a captura

1. Se o usuário já confirmou o slowdown em um caso pendente, retome seu
   `build/diagnostics/<caso>/case.json` e colete antes de abrir outro caso.
2. Identifique aparelho e título. Aproveite dados já fornecidos; se faltar,
   peça a cena/trecho onde a lentidão é visível enquanto prepara a captura.
   Não use performance do simulador como evidência de ganho no iPhone.
3. Confirme build com diagnósticos; se necessário compile/atualize segundo
   `ios/Vita3K/README.md`, mantendo o container e guardando binários/dSYM/UUIDs.
4. Execute `python3 ios/Vita3K/scripts/diagnostics.py arm --mode performance --device ID`
   (ou `--simulator UDID` para testes de infraestrutura). Guarde CASE na conversa.
   Preserve logs de uma execução anterior antes de reiniciar o app. O marcador
   só passa a valer na próxima inicialização; JIT pode precisar de reativação.
5. Execute `status --case CASE` após abrir o app. Confirme ACK novo no modo
   **performance**. Caso a abertura dependa do usuário, peça essa abertura e
   aguarde para verificar. Então peça para jogar até o trecho lento, permanecer
   aproximadamente 30–60 segundos e avisar a cena e o momento do slowdown.
   Aguarde a confirmação; não simule nem presuma uma reprodução humana.

## Coletar, medir e corrigir

1. Após a confirmação: `collect --case CASE`; rode
   `analyze DIRETORIO_DA_COLETA/diagnostics`. Leia também o runtime e os erros
   de coleta. Preserve as fontes antes de mexer em caches/configuração.
2. Correlacione intervalos guest/host, histogramas p50/p95/p99, spikes máximos,
   batches, render/present, pipeline, CPU do processo, memória e thermal/Low
   Power Mode. Tempos CPU incluem espera; não atribua automaticamente present
   lento à GPU. Não confunda reapresentações do host com FPS guest.
3. Isole o trecho de gameplay: exclua loading/pausas/background; distinga cache
   frio de quente. Compare mesma cena, resolução, config, build e condições
   térmicas. Quantifique também a interferência do diagnóstico antes de alegar
   ganhos pequenos. Não limpe cache nem mude FPS hack durante o baseline.
4. Se os agregados não localizarem o gargalo, use coleta dirigida com Instruments
   Time Profiler/Metal System Trace ou Tracy conectado. TRACE textual global,
   dumps de uniform e imports por chamada não são benchmark confiável.
5. Implemente a melhoria sustentada pelas medidas. Um hack específico deve
   usar title ID exato e revisão quando necessário, opção default desligada no
   sistema de custom config, persistência por jogo e possibilidade de desligar.
   Não habilite hacks por nome parcial nem altere defaults de outros jogos.
   Documente o compromisso visual/de precisão e não confunda FPS hack com
   otimização. Não adicione hack especulativo sem evidência do jogo.
6. Valide A/B no mesmo trecho; teste a opção desligada e um título fora do
   escopo para hacks. Compile e execute checks relevantes. Registre evidências,
   números, hipótese e mudanças em `CASE/analysis.md`; peça reteste humano se
   o ganho depende do aparelho ou cena inacessível.
7. Depois da coleta, `disarm --case CASE` desativa para o próximo boot. Nova
   reprodução instrumentada usa novo caso. Relate ganho medido e limitações;
   não declare melhoria de performance somente porque o build passou.
