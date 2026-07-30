# GRUT BIOS v0.1 — firmware/grut-node

Мінімальна прошивка GRUT BIOS для ESP8285. У цій версії:

- працює лише **BIOS** (завантаження, стартова діагностика, консоль);
- **Transport відключений** — підключено лише заглушку (`StubTransport`),
  яка нічого не робить і завжди повідомляє `enabled=no`;
- немає mesh, UDP, Wi-Fi AP чи автовизначення ролі — це навмисно,
  буде додано в наступних версіях.

## Вимоги

- Встановлений [Python 3](https://www.python.org/downloads/) (додається до PATH при інсталяції).
- Встановлений [PlatformIO Core (CLI)](https://platformio.org/install/cli).
- Плата на ESP8285, підключена по USB-UART перехіднику.

## Встановлення PlatformIO (один раз, Windows PowerShell)

```powershell
pip install -U platformio
```

Перевірка, що PlatformIO встановився:

```powershell
pio --version
```

## Визначення COM-порту

1. Підключіть плату USB-кабелем.
2. Відкрийте Диспетчер пристроїв (Device Manager) → "Порти (COM і LPT)".
3. Запам'ятайте номер порту, наприклад `COM5`.

Або через PlatformIO:

```powershell
pio device list
```

## Збірка прошивки

Перейдіть у папку проєкту (замініть шлях на свій):

```powershell
cd C:\GRUT\firmware\grut-node
pio run
```

Успішна збірка завершується рядком `[SUCCESS]` і показує розмір
прошивки (RAM/Flash).

## Прошивка плати

```powershell
pio run --target upload --upload-port COM5
```

Замініть `COM5` на реальний порт вашої плати.

Якщо плата на базі ESP8285 не входить у режим прошивки автоматично —
затисніть кнопку **FLASH/GPIO0**, коротко натисніть **RESET**, відпустіть
**RESET**, і лише після появи `Connecting........` у консолі відпустіть
**FLASH/GPIO0**.

## Підключення до консолі (монітор)

```powershell
pio device monitor --port COM5 --baud 57600
```

Після перезавантаження плати у консолі має з'явитись блок
`GRUT BIOS - startup diagnostics`, а потім запрошення `>`.

Команди консолі (набрати і натиснути Enter):

| Команда  | Опис                                   |
|----------|-----------------------------------------|
| `help`   | список команд                           |
| `status` | стан BIOS і Transport (uptime, heap...) |
| `reboot` | перезавантаження плати                  |

Вихід з монітора: `Ctrl+C`, потім `Ctrl+]` за потреби (залежно від
версії PlatformIO — зазвичай досить `Ctrl+C`).

## Очищення збірки (за потреби)

```powershell
pio run --target clean
```

## Технічні параметри (для довідки)

| Параметр      | Значення                    |
|---------------|------------------------------|
| Плата          | ESP8285                     |
| Flash          | 1 MB, режим DOUT             |
| Ld-script      | `eagle.flash.1m64.ld`        |
| UART (консоль) | 57600 8N1                    |
| Upload speed   | 460800                       |

## Структура проєкту

```
firmware/grut-node/
├── platformio.ini
├── include/
│   ├── bios/
│   │   ├── Bios.h            — клас BIOS (boot, консоль, статус)
│   │   └── Diagnostics.h     — стартова діагностика
│   └── transport/
│       ├── ITransport.h      — інтерфейс Transport (кордон BIOS/Transport)
│       └── StubTransport.h   — заглушка (Transport відключений)
├── src/
│   ├── main.cpp
│   ├── bios/
│   │   ├── Bios.cpp
│   │   └── Diagnostics.cpp
│   └── transport/
│       └── StubTransport.cpp
└── README_UA.md
```

## Що далі (не входить у v0.1)

- Реальна реалізація `ITransport` (ESP-NOW, UART-міст, UDP).
- Role manager (`AUTO/AIR/GROUND/RELAY`).
- Mesh-маршрутизація, TTL, CRC на рівні Transport.
