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
| MOSI | GPIO1 | SPIx_MOSI | ESP → STM |
| MISO | GPIO2 | SPIx_MISO | STM → ESP |
| SCLK | GPIO4 | SPIx_SCK | ESP → STM |
| CS/NSS | GPIO19 | SPIx_NSS | ESP → STM |
| DRDY | a definir | GPIO de saída/EXTI | STM → ESP |
| GND | GND | GND | comum |

Os GPIOs da ESP são provisórios e podem ser trocados posteriormente. O `DRDY` é recomendado; sem ele, a ESP precisa consultar `STATUS` periodicamente, aumentando consumo de SPI e latência.

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

A STM deve manter uma transação SPI escrava armada antes de elevar `DRDY`. Enquanto houver bloco pronto, `DRDY` permanece alto. Após uma leitura, se outro bloco já estiver pronto, `DRDY` pode permanecer alto; a task da ESP deve continuar lendo até o sinal baixar.

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

| Perfil | Base por divisão | Taxa por canal | Taxa total ADC (4 canais) | Frames/bloco | Payload |
|---|---|---:|---:|---:|---:|
| `FAST` (`0`) | 1–2 ms | 100 kS/s | 400 kconv/s | 256 | 2048 B |
| `MEDIUM` (`1`) | 5–10 ms | 20 kS/s | 80 kconv/s | 128 | 1024 B |
| `SLOW` (`2`) | 20–100 ms | 2 kS/s | 8 kconv/s | 64 | 512 B |
| `VERY_SLOW` (`3`) | 250 ms–1 s | 200 S/s | 800 conv/s | 16 | 128 B |

O perfil `FAST` não deve ultrapassar o limite real do ADC considerando resolução, sample time e impedância da fonte analógica. A proposta de 400 kconversões/s totais fica abaixo da capacidade típica do ADC do STM32G071 e também cabe com margem no SPI de 10 MHz.

A troca entre bases pertencentes ao mesmo perfil não deve reinicializar o ADC. Por exemplo, 20 ms/div e 100 ms/div permanecem em `SLOW`.

## Protocolo SPI

Todos os comandos são iniciados pela ESP. Os comandos de controle possuem 4 bytes:

```text
byte 0: opcode
byte 1: argumento 0
byte 2: argumento 1
byte 3: argumento 2
```

O conteúdo MISO durante comandos de controle pode ser ignorado. Para confirmação, a ESP faz uma leitura `STATUS` após o comando. Isso evita um protocolo de resposta complexo em transações de configuração pouco frequentes.

| Opcode | Nome | Argumentos | Ação STM |
|---:|---|---|---|
| `0x00` | `ALIVE` | `0xFF, 0xFF, 0xFF` | Responde assinatura e versão; não altera aquisição. |
| `0x01` | `CONFIG_PROFILE` | `profile, 0, 0` | Para/reconfigura timer, ADC e DMA com o perfil informado. |
| `0x02` | `START` | `0, 0, 0` | Inicia aquisição. |
| `0x03` | `STOP` | `0, 0, 0` | Para aquisição e baixa `DRDY`. |
| `0x04` | `STATUS` | `0xFF, 0xFF, 0xFF` | Retorna estado do escravo. |
| `0x10` | `READ_BLOCK` | seguido de dummies | Retorna o próximo bloco disponível. |
| `0x7F` | `RESET` | `0, 0, 0` | Limpa estado SPI e reinicia a aquisição parada. |

### Presença da placa (`ALIVE`)

Após inicializar o barramento SPI, a ESP envia uma transação de quatro bytes:

```text
TX: 0x00 0xFF 0xFF 0xFF
RX: ignorar RX[0]
    RX[1..3] = 0xA5 0x5A 0x01
```

`0xA5 0x5A` é a assinatura fixa e `0x01` é a versão deste protocolo. A STM
deve deixar essa resposta pronta antes de receber o opcode. A ESP só considera
a placa presente se os três bytes coincidirem exatamente; isso evita aceitar
MISO flutuante como resposta válida.

Se não houver resposta válida, a ESP informa que a placa STM32 não foi
detectada e não cria a task de aquisição SPI. Uma task leve de supervisão no
core 0 repete somente o `ALIVE` a cada 5 s. Quando a resposta aparecer, ela
cria a task de aquisição no core 0 e encerra a supervisão.

### Resposta `STATUS`

Na transação `STATUS`, a STM devolve quatro bytes após o byte de opcode:

```text
status | profile | last_seq | ready_blocks
```

- `status`: bit 0 = aquisição ativa; bit 1 = bloco disponível; bit 2 = overflow desde o último status.
- `profile`: perfil ADC ativo.
- `last_seq`: sequência do último bloco concluído.
- `ready_blocks`: número de blocos disponíveis, limitado ao tamanho da fila STM.

### Leitura de bloco

Para `READ_BLOCK`, a ESP baixa CS e transmite o opcode `0x10`, seguido de bytes dummy `0xFF` suficientes para receber todo o cabeçalho e payload do perfil ativo.

O primeiro byte recebido simultaneamente ao opcode deve ser ignorado. Os bytes seguintes são:

```text
seq | profile | frame_count_L | frame_count_H | payload
```

O payload contém `frame_count` frames de 8 bytes, em ordem cronológica, do mais antigo para o mais recente.

Exemplo `FAST`:

```text
TX: 0x10 + 2052 bytes 0xFF
RX: ignorar RX[0]
    RX[1] = seq
    RX[2] = profile
    RX[3..4] = frame_count = 256 (little-endian)
    RX[5..2052] = 256 × 8 bytes de frames
```

Não há timestamp nem CRC nesta versão. A ESP deve verificar se `seq` avançou de uma unidade módulo 256. Qualquer salto indica bloco perdido ou overflow na STM.

## Regras de software na ESP32

1. A task SPI deve ser criada com `xTaskCreatePinnedToCore(..., 0)`.
2. A task LVGL deve ficar fixada no core 1.
3. A ISR de `DRDY` apenas notifica a task SPI; ela não executa transferências.
4. A task SPI executa `READ_BLOCK`, valida cabeçalho/perfil/sequência e grava frames em uma fila SPSC.
5. O componente `osciloscopio`, no core 1, remove frames da fila e atualiza seu histórico circular em PSRAM.
6. Uma troca de perfil deve solicitar `STOP`, `CONFIG_PROFILE`, limpar a fila de entrada e enviar `START`.
7. O histórico visual da ESP não depende da base usada no instante de captura; a base de tempo controla apenas a janela apresentada.

## Regras de software na STM32

1. ADC e DMA não devem depender de interrupção por amostra; use timer + DMA por bloco.
2. O firmware deve manter o buffer que será transmitido estável enquanto CS estiver baixo.
3. `DRDY` só pode subir após o buffer SPI estar completamente armado.
4. A STM não deve bloquear a aquisição esperando a ESP, exceto quando os dois buffers já estiverem ocupados.
5. A sequência incrementa por bloco adquirido, inclusive quando um bloco precisa ser descartado por overflow.
6. `CONFIG_PROFILE`, `START`, `STOP` e `RESET` devem ser processados fora de interrupções longas, preservando a integridade do DMA e do SPI escravo.

## Próximas implementações na ESP

- Escolher e conectar o GPIO `DRDY`.
- Criar a task SPI no core 0 e a fila SPSC de frames.
- Expor no componente `osciloscopio` uma API de ingestão de frames ADC.
- Substituir progressivamente o gerador de sinal simulado pela entrada SPI.
