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
| `FAST` (`0`) | 400 kS/s | 100 kS/s | 800 kB/s | 256 | 2048 B |
| `MEDIUM` (`1`) | 100 kS/s | 25 kS/s | 200 kB/s | 128 | 1024 B |
| `SLOW` (`2`) | 50 kS/s | 12,5 kS/s | 100 kB/s | 64 | 512 B |
| `VERY_SLOW` (`3`) | 10 kS/s | 2,5 kS/s | 20 kB/s | 16 | 128 B |

O timer de trigger deve operar na taxa de frames indicada na tabela. Assim, no perfil `FAST`, TIM6 dispara uma sequência ADC de quatro canais a 100 kHz, resultando em 400 kconversões/s totais.

O perfil `FAST` não deve ultrapassar o limite real do ADC considerando resolução, sample time e impedância da fonte analógica. A proposta de 400 kconversões/s totais fica abaixo da capacidade típica do ADC do STM32G070 e gera 800 kB/s, deixando margem no SPI a 10 MHz (máximo teórico bruto de 1,25 MB/s).

A troca entre bases pertencentes ao mesmo perfil não deve reinicializar o ADC. Por exemplo, 20 ms/div e 100 ms/div permanecem em `SLOW`.

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
    RX[1..3] = 0xA5 0x5A 0x02
```

`0xA5 0x5A` é a assinatura fixa e `0x02` é a versão deste protocolo. A STM arma essa resposta depois de receber e validar `REQUEST_ALIVE`. A ESP só considera a placa presente se os três bytes coincidirem exatamente; isso evita aceitar MISO flutuante como resposta válida.

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
seq | profile | frame_count_L | frame_count_H | payload
```

O payload contém `frame_count` frames de 8 bytes, em ordem cronológica, do mais antigo para o mais recente.

Exemplo `FAST`:

```text
REQUEST_READ_BLOCK
TX: 0x10 0x00 0x00 0x00
RX: ignorar

RESPONSE_READ_BLOCK
TX: 0x80 + 2052 bytes 0xFF
RX: ignorar RX[0]
    RX[1] = seq
    RX[2] = profile
    RX[3..4] = frame_count = 256 (little-endian)
    RX[5..2052] = 256 × 8 bytes de frames
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

Dois slots absorvem jitter e reduzem perdas pontuais, mas não compensam uma diferença permanente de taxa. No perfil `FAST`, a ESP precisa consumir cada bloco de 256 frames em menos de aproximadamente 2,56 ms para acompanhar a produção contínua. A fila é margem de segurança, não substitui uma taxa SPI sustentada suficiente.

## Regras de software na ESP32

1. A task SPI deve ser criada com `xTaskCreatePinnedToCore(..., 0)`.
2. A task LVGL deve ficar fixada no core 1.
3. As ISRs de `DRDY` e `SYNC` apenas notificam a task SPI; elas não executam transferências.
4. Para cada operação, a task SPI envia uma `REQUEST`, aguarda `SYNC` alto, então envia `READ_RESPONSE` com o tamanho previsto para aquele opcode e aguarda `SYNC` baixo.
5. Quando `DRDY` estiver alto, a task SPI executa `REQUEST_READ_BLOCK` seguido de `RESPONSE_READ_BLOCK`, valida cabeçalho/perfil/sequência e grava frames em uma fila SPSC.
6. O componente `osciloscopio`, no core 1, remove frames da fila e atualiza seu histórico circular em PSRAM.
7. Uma troca de perfil deve solicitar `STOP`, `CONFIG_PROFILE`, limpar a fila de entrada e enviar `START`.
8. O histórico visual da ESP não depende da base usada no instante de captura; a base de tempo controla apenas a janela apresentada.

## Regras de software na STM32

1. ADC e DMA não devem depender de interrupção por amostra; use timer + DMA por bloco.
2. O firmware deve manter o buffer que será transmitido estável enquanto a transação DMA de resposta estiver armada ou em andamento.
3. `DRDY` só pode subir após existir um bloco completo e o firmware estar pronto para aceitar `REQUEST_READ_BLOCK`.
4. A STM deve armar DMA RX/TX em modo normal para cada `REQUEST` e cada `RESPONSE`; não deve iniciar transferências em ISR de CS.
5. A STM processa uma `REQUEST` somente depois que CS voltar a nível alto e a transferência de quatro bytes for validada. Em seguida, arma a `RESPONSE`, eleva `SYNC` e só baixa `SYNC` após a RESPONSE terminar e a próxima REQUEST estar armada.
6. A STM não deve bloquear a aquisição esperando a ESP, exceto quando os dois buffers já estiverem ocupados.
7. A sequência incrementa por bloco adquirido, inclusive quando um bloco precisa ser descartado por overflow.
8. `CONFIG_PROFILE`, `START`, `STOP` e `RESET` devem ser processados fora de interrupções longas, preservando a integridade do DMA e do SPI escravo.

## Próximas implementações na ESP

- Escolher e conectar o GPIO `DRDY`.
- Criar a task SPI no core 0 e a fila SPSC de frames.
- Expor no componente `osciloscopio` uma API de ingestão de frames ADC.
- Substituir progressivamente o gerador de sinal simulado pela entrada SPI.
