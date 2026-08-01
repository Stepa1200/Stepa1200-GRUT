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
| `transport status` | лише стан console/transport, без зміни володіння |
| `transport start`  | передати UART від консолі до Transport (консоль замовкає) |
| `transport stop`   | повернути UART консолі |

**Важливо:** після `transport start`, поки консоль не отримає UART назад,
консоль **не реагує на жодне введення** — це навмисно (UART ексклюзивно
належить Transport, поки він активний). Щоб знову побачити `>` і
`help`/`status`, потрібен або `transport stop` (якщо є інший канал, щоб
його надіслати — в v0.1 такого немає), або просто перезавантаження
плати (`reboot` викликаний фізично / by power cycle скидає стан назад
до `console=running`).

Вихід з монітора: `Ctrl+C`, потім `Ctrl+]` за потреби (залежно від
версії PlatformIO — зазвичай досить `Ctrl+C`).

## Тести (host-side, без плати)

```powershell
pio test -e native
```

Запускає обидва набори тестів:
- `test_command_parser` — розбір консольних команд (`help`/`status`/`reboot`),
  логіка в `lib/bios_command_parser` (без залежності від Arduino).
- `test_runtime_manager` — порядок переходу `console.stop() → transport.start()`
  і навпаки, rollback при невдалому старті Transport, логіка в
  `lib/runtime_manager` (теж без залежності від Arduino).

Успішний запуск покаже `PASSED` для обох наборів.

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
├── platformio.ini              — env:esp8285 (плата) + env:native (тести)
├── include/
│   ├── grut/
│   │   └── PhysicalUart.h     — спільний бодрейт для UartConsole/UartTransport
│   ├── bios/
│   │   ├── Bios.h             — клас BIOS (boot, консоль, статус)
│   │   ├── IConsole.h         — інтерфейс консолі (start/stop/isRunning + I/O)
│   │   ├── UartConsole.h      — консоль над фізичним Serial (fallback)
│   │   └── Diagnostics.h      — стартова діагностика (пише через IConsole)
│   └── transport/
│       ├── ITransport.h      — інтерфейс Transport (симетричний IConsole)
│       ├── UartTransport.h   — реальний Transport, володіє фізичним UART
│       └── StubTransport.h   — завжди вимкнений (шаблон для майбутніх лінків)
├── lib/
│   ├── bios_command_parser/  — чиста логіка розбору команд, без Arduino
│   │   ├── CommandParser.h
│   │   └── CommandParser.cpp
│   └── runtime_manager/      — координує ексклюзивне володіння UART, без Arduino
│       ├── RuntimeManager.h
│       └── RuntimeManager.cpp
├── src/
│   ├── main.cpp               — UartConsole + UartTransport + RuntimeManager
│   ├── bios/
│   │   ├── Bios.cpp
│   │   ├── UartConsole.cpp    — єдине місце в BIOS, що торкається Serial
│   │   └── Diagnostics.cpp
│   └── transport/
│       ├── UartTransport.cpp  — єдине місце в Transport, що торкається Serial
│       └── StubTransport.cpp
├── test/
│   ├── test_command_parser/
│   │   └── test_main.cpp     — host-side тести парсингу команд
│   └── test_runtime_manager/
│       └── test_main.cpp     — host-side тести порядку console/transport
└── README_UA.md
```

## Архітектурне правило: ексклюзивне володіння UART

`UartConsole` і `UartTransport` ніколи не працюють одночасно — обидва
претендують на той самий фізичний `Serial`. `RuntimeManager`
(`lib/runtime_manager`) гарантує порядок переходу:

- `enableTransport()`: спочатку `console.stop()`, потім `transport.start()`.
  Якщо `transport.start()` не вдався — консоль автоматично відновлюється
  (rollback), BIOS ніколи не залишається без жодного інтерфейсу.
- `disableTransport()`: спочатку `transport.stop()`, потім `console.start()`.

У поточному milestone `RuntimeManager` створений у `main.cpp`, але ще
нічого не викликає `enableTransport()`/`disableTransport()` — тригера
для активації Transport поки немає (окремий майбутній milestone). Плата
й далі завантажується так само, як раніше: консоль активна, Transport
зупинений.

## Що далі (не входить у v0.1)

- Реальна реалізація `ITransport` (ESP-NOW, UART-міст, UDP).
- Role manager (`AUTO/AIR/GROUND/RELAY`).
- Mesh-маршрутизація, TTL, CRC на рівні Transport.
