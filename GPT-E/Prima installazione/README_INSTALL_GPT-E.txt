# Installer GPT-E completo

## Prima installazione sul Raspberry

```bash
chmod +x install_gpte_complete.sh
./install_gpte_complete.sh
```

Durante l'installazione ti viene chiesta solo `OPENAI_API_KEY`.
Se non ce l'hai pronta, premi INVIO e la puoi inserire dopo.

`GPTE_CORE_TOKEN` viene generato automaticamente: non devi cercarlo, non devi copiarlo da siti esterni.

## Dopo l'installazione

```bash
cd ~/GPT-E
./gpte_help.sh
```

La guida ti mostra:
- se i servizi sono attivi
- se manca la chiave OpenAI
- l'IP rilevato del Raspberry
- i comandi per test, chat, log e riavvio

## Comandi utili

```bash
cd ~/GPT-E
source env/bin/activate
python test_status.py
python chat_cli.py
```

Servizi installati:
- `gpte-core.service`: UART, stato, heartbeat e comandi verso ESP32 testa
- `gpte.service`: brain AI, TTS e stream audio su porta 5001
- `gpte-control.service`: readiness e shutdown per il telecomando su porta 5000

Mappatura inclusa:
- PWM12 = servo asse sinistra
- PWM13 = servo asse destra
- PWM14 = collo destra/sinistra
- PWM15 = collo su/giu
- Occhio SX: R=PWM3, G=PWM4, B=PWM5
- Occhio DX: R=PWM9, G=PWM10, B=PWM11
