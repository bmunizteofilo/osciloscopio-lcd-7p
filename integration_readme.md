# Integração ESP32-S3 ↔ STM32G071 — aquisição do osciloscópio

Este documento define a primeira versão do protocolo entre a placa ESP32-S3 com LCD e a Nucleo STM32G071 responsável pela aquisição ADC.

O objetivo é manter a comunicação rápida, previsível e simples: a STM captura amostras por ADC + DMA; a ESP solicita blocos prontos via SPI e alimenta o histórico do osciloscópio.

## Papéis e arquitetura

- ESP32-S3: mestre SPI, interface gráfica LVGL e armazenamento de histórico.
- STM32G071: escravo SPI, temporizador de amostragem, ADC e DMA.
- A STM nunca controla o sinal CS. Somente o mestre ESP32 controla CS/NSS.
- Um GPIO adicional `DRDY` da STM para a ESP sinaliza que existe ao menos um bloco completo disponível.

```text
STM32: Timer → ADC scan → DMA ping-pong → bloco pronto → DRDY alto
ESP32 core 0: ISR DRDY → task SPI → READ_BLOCK → fila SPSC
ESP32 core 1: osciloscópio consome fila → histórico PSRAM → LVGL
```

O histórico e o LVGL permanecem exclusivamente no core 1. A task SPI, fixada no core 0, não deve chamar APIs LVGL nem escrever diretamente nos buffers de histórico do osciloscópio.

## Ligações físicas

Configuração provisória atual da ESP32-S3:

| Sinal | ESP32-S3 | STM32G071 | Direção |
|---|---:|---|---|
| MOSI | GPIO41 | SPIx_MOSI | ESP → STM |
| MISO | GPIO4 | SPIx_MISO | STM → ESP |
| SCLK | GPIO1 | SPIx_SCK | ESP → STM |
| CS/NSS | GPIO20 | SPIx_NSS | ESP → STM |
| DRDY | a definir | GPIO de saída/EXTI | STM → ESP |
| SYNC | GPIO42 | GPIO de saída/EXTI | STM → ESP |
| GND | GND | GND | comum |

Os GPIOs da ESP são provisórios e podem ser trocados posteriormente. O `DRDY` é recomendado; sem ele, a ESP precisa consultar `STATUS` periodicamente, aumentando consumo de SPI e latência. O `SYNC` é obrigatório para eliminar atrasos artificiais entre as duas fases de cada comando.

## Configuração SPI

| Item | Valor |
|---|---|
| Mestre | ESP32-S3 (`SPI3_HOST`) |
| Escravo | STM32G071 |
| Clock inicial | 10 MHz |
| Modo | SPI mode 0 (`CPOL=0`, `CPHA=0`) |
| Ordem de bits | MSB first |
| Dummy de leitura | `0xFF` |
| CS | ativo em nível baixo |
| Transferência máxima ESP | 4096 bytes |

A STM deve manter uma transação SPI escrava por DMA armada antes de aceitar qualquer transferência. O protocolo usa duas transações separadas por `CS` alto: uma requisição de quatro bytes e uma resposta já preparada pela STM. Enquanto houver bloco pronto, `DRDY` permanece alto. Após uma leitura, se outro bloco já estiver pronto, `DRDY` pode permanecer alto; a task da ESP deve continuar lendo até o sinal baixar.

## Aquisição ADC na STM32

A STM deve usar um timer para disparar uma sequência regular de quatro canais ADC e DMA em modo circular/ping-pong.

Cada frame representa uma varredura ADC:

```text
frame = CH1 | CH2 | CH3 | CH4
```

Cada canal ocupa `uint16_t` little-endian. Os 12 bits válidos da conversão ficam nos bits menos significativos; não há empacotamento de 12 bits.

```text
CH1_L CH1_H CH2_L CH2_H CH3_L CH3_H CH4_L CH4_H
```

Um frame tem exatamente 8 bytes. Os canais são sequenciais dentro de uma varredura ADC, portanto não são estritamente simultâneos; a taxa de conversão da STM deve ser suficiente para tornar esse desfasamento irrelevante para a banda medida.

Use dois buffers DMA/tx alternados. Ao concluir um bloco, a STM o disponibiliza para SPI, incrementa a sequência e prepara o outro buffer para a aquisição seguinte. Se o mestre não consumir um bloco antes de não haver mais espaço disponível, a STM pode descartar o bloco mais antigo; a lacuna será detectada pelo byte de sequência.

## Perfis de aquisição

A base de tempo selecionada na ESP define um dos quatro perfis. A STM só deve trocar a frequência de timer/ADC após receber `CONFIG_PROFILE`.

Neste documento, **taxa total ADC** significa o número de conversões de todos os canais por segundo. Como cada frame possui quatro conversões de 16 bits, valem sempre as relações:

```text
taxa por canal = taxa total ADC / 4
taxa de frames = taxa por canal
bytes por segundo = taxa de frames × 8
```

| Perfil | Taxa total ADC | Taxa por canal / frames | Dados brutos | Frames/bloco | Payload |
|---|---:|---:|---:|---:|---:|
| `FAST` (`0`) | 400 kS/s | 100 kS/s | 800 kB/s | reservado na STM32 | não enviado pela ESP |
| `MEDIUM` (`1`) | 100 kS/s | 25 kS/s | 200 kB/s | 256 | 2048 B |
| `SLOW` (`2`) | 50 kS/s | 12,5 kS/s | 100 kB/s | 128 | 1024 B |
| `VERY_SLOW` (`3`) | 10 kS/s | 2,5 kS/s | 20 kB/s | 64 | 512 B |

O timer de trigger deve operar na taxa de frames indicada na tabela. Assim, no perfil `FAST`, TIM6 dispara uma sequência ADC de quatro canais a 100 kHz, resultando em 400 kconversões/s totais.

O perfil `FAST` não deve ultrapassar o limite real do ADC considerando resolução, sample time e impedância da fonte analógica. A proposta de 400 kconversões/s totais fica abaixo da capacidade típica do ADC do STM32G070 e gera 800 kB/s, deixando margem no SPI a 10 MHz (máximo teórico bruto de 1,25 MB/s).

### Mapeamento de base de tempo na ESP

| Base por divisão | Perfil | Taxa de frames | Frames por pixel, aproximadamente |
|---|---:|---:|---:|
| 1 ms, 2 ms, 5 ms e 10 ms | `MEDIUM` (`1`) | 25 kframes/s | 0,4 a 4,3 |
| 20 ms | `SLOW` (`2`) | 12,5 kframes/s | 4,3 |
| 50 ms, 100 ms, 250 ms, 500 ms e 1 s | `VERY_SLOW` (`3`) | 2,5 kframes/s | 2,1 ou mais |

O objetivo é manter resolução temporal suficiente sem sobrecarregar ADC, SPI e CPU. Em 1 ms/div, 25 kframes/s produzem 250 amostras na janela de 10 ms: cada amostra representa 40 us e um pulso mínimo de 1 ms contém 25 amostras. Quando há menos amostras que pixels, a ESP distribui os pontos pelo eixo X e usa interpolação linear apenas para continuidade visual; ela não cria informação elétrica adicional.

O perfil `FAST` permanece reservado na STM32, mas não é selecionado pela UI nem enviado pela ESP. Mesmo com blocos de 512 frames, o protocolo SPI request/response a 10 MHz com fios sustentou apenas aproximadamente 58 kframes/s, abaixo dos 100 kframes/s necessários.

Ao trocar de base, a ESP reinicia ADC/DMA no perfil correspondente e descarta o histórico anterior. A STM deve aceitar `STOP`, `CONFIG_PROFILE` e `START` a qualquer momento entre blocos.

### Alterações obrigatórias na STM32: blocos de aquisição

Os tamanhos dos blocos foram ajustados para reduzir a taxa de handshakes SPI. Portanto, no firmware STM32, a função que mapeia perfil para `frame_count` deve usar:

```c
case APP_ACQUISITION_PROFILE_MEDIUM:    *frame_count = 256U; break;
case APP_ACQUISITION_PROFILE_SLOW:      *frame_count = 128U; break;
case APP_ACQUISITION_PROFILE_VERY_SLOW: *frame_count = 64U; break;
```

Consequências dos novos blocos:

- `MEDIUM`: 256 frames, payload de 2048 bytes, período de 10,24 ms e aproximadamente 98 blocos/s;
- `SLOW`: 128 frames, payload de 1024 bytes, período de 10,24 ms e aproximadamente 98 blocos/s;
- `VERY_SLOW`: 64 frames, payload de 512 bytes, período de 25,6 ms e aproximadamente 39 blocos/s.

Essas mudanças reduzem o número de handshakes `DRV`/`SYNC` e transações SPI, mantendo exatamente as mesmas taxas de amostragem e qualidade de sinal.

O FAST de 512 frames pode permanecer implementado somente na STM32 para experimentos futuros:

```c
#define APP_ADC_MAX_FRAMES_PER_BLOCK  (512U)
case APP_ACQUISITION_PROFILE_FAST: *frame_count = 512U; break;
```

`APP_SPI_MAX_ADC_PAYLOAD_BYTES` deve ser ao menos `4096U`. Os buffers TX e RX SPI devem suportar no mínimo **4102 bytes**: 1 byte simultâneo, 5 bytes de cabeçalho e 4096 bytes de payload. A ESP não solicita esse perfil atualmente.

## Acionamento PWM dos bicos

A STM controla quatro bicos em sequência (`Bico 1 → Bico 2 → Bico 3 → Bico 4`).
Nunca há mais de um bico em nível ativo: o desligamento de um bico dispara por
hardware o próximo. O tempo desligado de cada bico continua transcorrendo
enquanto os demais bicos são atendidos.

O comando de limpeza deve informar RPM, tempo máximo ligado (`Ton`) em
milissegundos inteiros, quantidade de ciclos completos e pausa entre ciclos. Um ciclo completo equivale a uma
cadeia dos quatro bicos; portanto, `cycles = 100` produz 100 pulsos em cada
bico, totalizando 400 pulsos.

### Configuração PWM pela SPI

A `REQUEST` SPI tem sempre quatro bytes. Por isso os nove bytes da
configuração PWM são enviados em três comandos de configuração, seguidos por
um comando explícito de início. Todos os campos multibyte são little-endian.
A ESP deve esperar a `RESPONSE` de sucesso de cada comando antes de enviar o
seguinte.

```text
PWM_CONFIG_0 (0x20): RPM_L | RPM_H | Ton_ms
PWM_CONFIG_1 (0x21): cycles_L | cycles_H | pause_ms_L
PWM_CONFIG_2 (0x22): pause_ms_H | operation_mode | 0x00
PWM_START    (0x23): 0x00 | 0x00 | 0x00
PWM_STOP     (0x24): 0x00 | 0x00 | 0x00
CYCLE_PAUSE  (0x25): 0x00 | 0x00 | 0x00
CYCLE_RESUME (0x26): 0x00 | 0x00 | 0x00
```

`Ton_ms` é um `uint8_t`, `RPM`, `cycles` e `pause_ms` são `uint16_t`. A
pausa aumenta o intervalo entre dois inícios sucessivos do Bico 1; ela não
altera a validação do máximo `Ton` permitido pelo RPM.

`operation_mode` é sempre um `uint8_t`:

```text
0       = teste manual: ignora cycles e permanece ativo até PWM_STOP
1       = execução finita: respeita cycles
2..255  = testes automáticos; cada modo pode definir uma receita STM32
```

Os modos automáticos `0x06` e `0x09` são testes de rotações: a ESP envia o RPM
inicial, `3 ms`, `pause_ms = 0` e `cycles = 1` apenas para completar a configuração
serializada. A STM executa internamente as receitas abaixo; o limite de ciclos é
ignorado nesses modos e a execução termina pelo tempo:

```text
0x06: uma rampa linear de 350 a 5000 RPM em 30 segundos
0x09: duas rampas lineares de 350 a 6300 RPM, 40 segundos cada (80 s total)
0x0A: cinco rampas lineares de 350 a 5000 RPM, 19 segundos cada (95 s total)
```

A alteração do período é aplicada somente entre cadeias completas dos quatro bicos.

`PWM_START` só é aceito depois de `PWM_CONFIG_0`, `PWM_CONFIG_1` e
`PWM_CONFIG_2`. Uma execução finita aceita de 1 a 10.000 ciclos por partida.
`PWM_STOP` cancela a rotina PWM e não para a aquisição ADC.

`CYCLE_PAUSE` para simultaneamente ADC, DMA e PWM, limpa os blocos ADC pendentes
e preserva o contador de ciclos já concluídos. `CYCLE_RESUME` retoma ADC e PWM
com a mesma configuração, continuando a contagem do ciclo interrompido. Ambos
são aceitos somente durante uma execução compatível com seu estado.

### Regra de RPM, duty e tempo ligado

O RPM representa um motor quatro-tempos. Cada bico se repete a cada duas voltas
do virabrequim. A ESP **deve** validar a regra antes de enviar o comando, e a
STM a valida novamente antes de programar os timers.

```text
1 <= RPM <= 10.000

T_ciclo       = 120.000.000 / RPM us
T_on_duty_50  = T_ciclo / 2 = 60.000.000 / RPM us
T_on_serial   = T_ciclo / 4 = 30.000.000 / RPM us
T_on_maximo   = min(35.000 us, T_on_duty_50, T_on_serial)
              = min(35.000 us, 30.000.000 / RPM us)

Ton_us = Ton_ms * 1.000
1 ms <= Ton solicitado <= 35 ms
Ton_us <= T_on_maximo
```

`T_on_duty_50` representa o limite do duty original de 50%. `T_on_serial` é o
limite adicional imposto pela fonte: os quatro pulsos não podem se sobrepor e
precisam caber no mesmo `T_ciclo`. Por isso ele é o limite efetivo mais restrito.
Um pedido fora desses limites deve receber resposta de argumento inválido; a STM
não deve reduzir `Ton` silenciosamente nem iniciar a limpeza.

Exemplo para `RPM = 5.000`:

```text
T_ciclo = 24.000 us
T_on_duty_50 = 12.000 us
T_on_serial = 6.000 us
T_on_maximo = 6.000 us
```

Assim, `Ton = 6 ms` é aceito e `Ton = 7 ms` é rejeitado. O tempo em nível baixo
de cada bico não precisa terminar antes de iniciar o próximo; basta que o bico
atual já tenha sido desligado.

## Protocolo SPI

Todos os comandos são iniciados pela ESP e possuem duas fases: `REQUEST` e `RESPONSE`. Esta separação é obrigatória porque, em SPI escravo, a STM precisa receber o opcode, processá-lo e armar o DMA de transmissão **antes** de o mestre gerar os clocks da resposta.

### Fase `REQUEST`

A ESP baixa `CS`, transmite exatamente quatro bytes e volta a elevar `CS`:

```text
byte 0: opcode
byte 1: argumento 0
byte 2: argumento 1
byte 3: argumento 2
```

O conteúdo MISO durante `REQUEST` é ignorado. A STM valida que a transação teve exatamente quatro bytes, processa o comando fora da interrupção de CS e arma a fase `RESPONSE` correspondente.

### Fase `RESPONSE`

Após a STM ter preparado a resposta, ela eleva `SYNC`. A ESP detecta a borda de subida por interrupção e somente então inicia uma nova transação: baixa `CS`, envia o opcode fixo `0x80` (`READ_RESPONSE`) seguido de bytes dummy `0xFF` e eleva `CS` ao final. O primeiro byte recebido simultaneamente a `0x80` é sempre ignorado; os bytes seguintes formam a resposta do comando anterior.

O tamanho da resposta é definido pelo opcode da requisição anterior e pelo perfil já conhecido pela ESP. Depois de receber toda a `RESPONSE`, a ESP aguarda `SYNC` voltar a nível baixo antes de enviar uma nova `REQUEST`. A STM baixa `SYNC` somente depois de processar o fim da resposta e rearmar o DMA para a próxima `REQUEST`.

### Sincronismo `SYNC`

`SYNC` resolve a corrida entre o término de uma transação SPI e o armamento DMA da fase seguinte na STM32. Ele não substitui `DRDY` e não indica disponibilidade de dados ADC.

```text
ESP: REQUEST (CS baixo -> alto)
STM: processa REQUEST, prepara RESPONSE e arma DMA
STM: SYNC sobe
ESP: interrupção SYNC -> READ_RESPONSE (CS baixo -> alto)
STM: processa fim da RESPONSE, rearma DMA de REQUEST e SYNC desce
ESP: aguarda SYNC baixo antes da próxima REQUEST
```

Regras elétricas e de firmware:

- `SYNC` permanece baixo em repouso e durante uma `REQUEST`.
- A STM o eleva apenas quando a `RESPONSE` correspondente está integralmente armada em DMA.
- A ESP o configura como entrada com interrupção nas duas bordas; a ISR apenas notifica a task SPI no core 0.
- A STM o baixa somente depois de receber o fim da `RESPONSE` e rearmar a próxima `REQUEST`.
- Se `SYNC` não subir ou não descer dentro do timeout definido pela ESP, a transação falha e a comunicação deve ser reinicializada de forma segura.

| Opcode | Nome | Argumentos | Ação STM |
|---:|---|---|---|
| `0x00` | `ALIVE` | `0xFF, 0xFF, 0xFF` | Prepara assinatura e versão. Resposta: 3 bytes. |
| `0x01` | `CONFIG_PROFILE` | `profile, 0, 0` | Para/reconfigura timer, ADC e DMA com o perfil informado. Resposta: 1 byte de resultado. |
| `0x02` | `START` | `0, 0, 0` | Inicia aquisição. Resposta: 1 byte de resultado. |
| `0x03` | `STOP` | `0, 0, 0` | Para aquisição e baixa `DRDY`. Resposta: 1 byte de resultado. |
| `0x04` | `STATUS` | `0xFF, 0xFF, 0xFF` | Prepara o estado do escravo. Resposta: 4 bytes. |
| `0x10` | `READ_BLOCK` | `0, 0, 0` | Trava o próximo bloco disponível e prepara cabeçalho e payload. Só permitido com `DRDY=1`. |
| `0x20` | `PWM_CONFIG_0` | `rpm_L, rpm_H, Ton_ms` | Grava RPM e tempo ligado PWM. Resposta: 1 byte de resultado. |
| `0x21` | `PWM_CONFIG_1` | `cycles_L, cycles_H, pause_ms_L` | Grava ciclos e byte baixo da pausa PWM. Resposta: 1 byte de resultado. |
| `0x22` | `PWM_CONFIG_2` | `pause_ms_H, operation_mode, 0` | Grava byte alto da pausa e modo PWM. Resposta: 1 byte de resultado. |
| `0x23` | `PWM_START` | `0, 0, 0` | Valida a configuração completa e inicia os bicos. Resposta: 1 byte de resultado. |
| `0x24` | `PWM_STOP` | `0, 0, 0` | Para imediatamente a rotina PWM. Resposta: 1 byte de resultado. |
| `0x25` | `CYCLE_PAUSE` | `0, 0, 0` | Pausa ADC e PWM, preservando o contador de ciclos. Resposta: 1 byte de resultado. |
| `0x26` | `CYCLE_RESUME` | `0, 0, 0` | Retoma ADC e PWM a partir do contador preservado. Resposta: 1 byte de resultado. |
| `0x7F` | `RESET` | `0, 0, 0` | Limpa estado SPI e reinicia a aquisição parada. Resposta: 1 byte de resultado. |
| `0x80` | `READ_RESPONSE` | seguido de dummies | Usado somente na fase `RESPONSE`; não é uma `REQUEST`. |

Para as respostas de um byte, o valor é `0x00` para sucesso ou um código de erro não nulo. Os códigos de erro serão definidos junto da implementação do driver e da máquina de estados.

### Presença da placa (`ALIVE`)

Após inicializar o barramento SPI, a ESP envia `REQUEST_ALIVE` e, após `CS` subir, lê `RESPONSE_ALIVE`:

```text
REQUEST_ALIVE
TX: 0x00 0xFF 0xFF 0xFF
RX: ignorar

RESPONSE_ALIVE
TX: 0x80 0xFF 0xFF 0xFF
RX: ignorar RX[0]
    RX[1..3] = 0xA5 0x5A 0x03
```

`0xA5 0x5A` é a assinatura fixa e `0x03` é a versão deste protocolo. A STM arma essa resposta depois de receber e validar `REQUEST_ALIVE`. A ESP só considera a placa presente se os três bytes coincidirem exatamente; isso evita aceitar MISO flutuante como resposta válida.

Se não houver resposta válida, a ESP informa que a placa STM32 não foi
detectada e não cria a task de aquisição SPI. Uma task leve de supervisão no
core 0 repete somente o `ALIVE` a cada 5 s. Quando a resposta aparecer, ela
cria a task de aquisição no core 0 e encerra a supervisão.

### Resposta `STATUS`

Após `REQUEST_STATUS`, a ESP executa `READ_RESPONSE` com quatro bytes dummy. A STM devolve quatro bytes após o byte `0x80`:

```text
status | profile | last_seq | ready_blocks
```

- `status`: bit 0 = aquisição ativa; bit 1 = bloco disponível; bit 2 = overflow desde o último status.
- `profile`: perfil ADC ativo.
- `last_seq`: sequência do último bloco concluído.
- `ready_blocks`: número de blocos disponíveis, limitado ao tamanho da fila STM.

### Leitura de bloco

Para `READ_BLOCK`, a ESP só inicia `REQUEST_READ_BLOCK` quando `DRDY` estiver alto. Depois de a STM validar a requisição, travar o bloco mais antigo e armar o DMA, a ESP executa `READ_RESPONSE` com bytes dummy `0xFF` suficientes para receber todo o cabeçalho e payload do perfil ativo.

O primeiro byte recebido simultaneamente ao opcode deve ser ignorado. Os bytes seguintes são:

```text
seq | profile | frame_count_L | frame_count_H | cycle_done | payload
```

O payload contém `frame_count` frames de 8 bytes, em ordem cronológica, do mais antigo para o mais recente.

`cycle_done` é um `uint8_t` associado ao bloco ADC:

```text
0 = não houve encerramento PWM finito pendente neste bloco
1 = a quantidade de ciclos PWM configurada terminou
```

Quando uma execução PWM finita termina, a STM mantém a indicação pendente até
conseguir publicar um bloco ADC na fila SPI. Ela marca somente esse bloco com
`cycle_done = 1`; blocos posteriores voltam a `0`. Em modo manual e após
`PWM_STOP`, o campo permanece em `0`. O comportamento de `DRDY` não muda:
ele continua indicando exclusivamente que há ao menos um bloco ADC pronto.

Exemplo `FAST`:

```text
REQUEST_READ_BLOCK
TX: 0x10 0x00 0x00 0x00
RX: ignorar

RESPONSE_READ_BLOCK
TX: 0x80 + 2053 bytes 0xFF
RX: ignorar RX[0]
    RX[1] = seq
    RX[2] = profile
    RX[3..4] = frame_count = 256 (little-endian)
    RX[5] = cycle_done
    RX[6..2053] = 256 × 8 bytes de frames
```

Não há timestamp nem CRC nesta versão. A ESP deve verificar se `seq` avançou de uma unidade módulo 256. Qualquer salto indica bloco perdido ou overflow na STM.

## Buffers ADC e SPI

Os buffers do ADC e os buffers pendentes para SPI têm responsabilidades diferentes e devem ser independentes na STM32.

### ADC: DMA ping-pong

O ADC deve operar com DMA circular dividido em duas metades (ping-pong). Enquanto o DMA preenche uma metade, a outra metade concluída pode ser copiada pela aplicação para a fila SPI. A aplicação não deve transmitir diretamente uma região do DMA que possa voltar a ser sobrescrita pelo ADC.

### SPI: fila de ao menos dois blocos

A camada SPI deve manter uma fila de pelo menos dois blocos completos, cada um contendo payload, tamanho, perfil, quantidade de frames e sequência. Isso permite que:

1. Um bloco esteja sendo enviado pela resposta DMA SPI.
2. O bloco ADC seguinte seja copiado para o segundo slot da fila.
3. Pequenas variações de latência da ESP, de DMA ou de interrupções não causem perda imediata.

Ao processar `READ_BLOCK`, a STM copia o bloco mais antigo da fila para o buffer estável de transmissão SPI e arma a `RESPONSE`. Assim que essa cópia terminar, o slot de fila de origem deve ser liberado para receber um novo bloco ADC; ele não deve permanecer ocupado até o fim da transferência SPI, porque o buffer TX já possui uma cópia estável dos dados.

`DRDY` deve permanecer alto enquanto existir ao menos um bloco na fila e só deve baixar quando ela ficar vazia. Se a fila estiver cheia ao chegar um novo bloco ADC, a STM pode descartar o mais antigo ou o mais novo, desde que incremente a sequência normalmente; a ESP detectará a lacuna pelo byte `seq`.

Dois slots absorvem jitter e reduzem perdas pontuais, mas não compensam uma diferença permanente de taxa. `MEDIUM` e `SLOW` possuem intervalo de 10,24 ms, e `VERY_SLOW` de 25,6 ms. A fila é margem de segurança, não substitui uma taxa SPI sustentada suficiente.

## Regras de software na ESP32

1. A task SPI deve ser criada com `xTaskCreatePinnedToCore(..., 0)`.
2. A task LVGL deve ficar fixada no core 1.
3. As ISRs de `DRDY` e `SYNC` apenas notificam a task SPI; elas não executam transferências.
4. Para cada operação, a task SPI envia uma `REQUEST`, aguarda `SYNC` alto, então envia `READ_RESPONSE` com o tamanho previsto para aquele opcode e aguarda `SYNC` baixo.
5. Quando `DRDY` estiver alto, a task SPI executa `REQUEST_READ_BLOCK` seguido de `RESPONSE_READ_BLOCK`, valida cabeçalho/perfil e grava o payload diretamente no histórico circular em PSRAM.
6. O Core 0 é o único escritor do histórico; o Core 1 lê apenas snapshots atômicos para renderizar. Portanto, lentidão do LVGL nunca pode interromper a drenagem SPI.
7. Uma troca de perfil deve solicitar `STOP`, `CONFIG_PROFILE`, resetar o histórico e enviar `START`.
8. A interface desenha em taxa visual limitada, mas a captura e o histórico preservam todos os frames recebidos.

## Regras de software na STM32

1. ADC e DMA não devem depender de interrupção por amostra; use timer + DMA por bloco.
2. O firmware deve manter o buffer que será transmitido estável enquanto a transação DMA de resposta estiver armada ou em andamento.
3. `DRDY` só pode subir após existir um bloco completo e o firmware estar pronto para aceitar `REQUEST_READ_BLOCK`.
4. A STM deve armar DMA RX/TX em modo normal para cada `REQUEST` e cada `RESPONSE`; não deve iniciar transferências em ISR de CS.
5. A STM processa uma `REQUEST` somente depois que CS voltar a nível alto e a transferência de quatro bytes for validada. Em seguida, arma a `RESPONSE`, eleva `SYNC` e só baixa `SYNC` após a RESPONSE terminar e a próxima REQUEST estar armada.
6. A STM não deve bloquear a aquisição esperando a ESP, exceto quando os dois buffers já estiverem ocupados.
7. A sequência incrementa por bloco adquirido, inclusive quando um bloco precisa ser descartado por overflow.
8. `CONFIG_PROFILE`, `START`, `STOP`, comandos `PWM_*` e `RESET` devem ser processados fora de interrupções longas, preservando a integridade do DMA e do SPI escravo.

## Próximas implementações na ESP

- Escolher e conectar o GPIO `DRDY`.
- Monitorar a sequência dos blocos para registrar lacunas de aquisição.
- Validar as quatro faixas de taxa com gerador de função e sinais reais.
