# Installer GPT-E completo

Uso sul Raspberry:

```bash
chmod +x install_gpte_complete.sh
./install_gpte_complete.sh
```

Dopo l'installazione:

```bash
cd ~/GPT-E
nano .env
# inserisci OPENAI_API_KEY
source env/bin/activate
python test_status.py
python chat_cli.py
```

Mappatura inclusa:
- PWM12 = servo asse sinistra
- PWM13 = servo asse destra
- PWM14 = collo destra/sinistra
- PWM15 = collo su/giù
- Occhio SX: R=PWM3, G=PWM4, B=PWM5
- Occhio DX: R=PWM9, G=PWM10, B=PWM11
