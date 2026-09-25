#include "core/I18nTable.h"

// Russian interface text.
//
// Keys are the English source strings, spelled here exactly as the call sites
// spell them -- a key that differs by one space is a key that never matches,
// and the only symptom is a paragraph that stays English. A test in the
// repository checks every key below against the sources so that mismatch is
// found by the build rather than by a reader.
//
// Format specifiers are part of the contract, not part of the prose: a
// translation that loses a %s or reorders two of them against their arguments
// is a crash, so each one keeps the same specifiers in the same order.
//
// Not translated, deliberately: file names, the add-on's own key names, the
// names of NVIDIA's runtimes, and the log. The first three are identifiers a
// reader has to be able to match against what is on their disk; the log is
// what a bug report carries back to the maintainer.

namespace sidecar {

const TranslationPair kRussianTable[] = {
    // ---------------------------------------------------------------- shell
    {"Status", "Состояние"},
    {"Setup", "Установка"},
    {"Checks", "Проверки"},
    {"Tuning", "Настройка"},
    {"Log", "Журнал"},

    {"Neural rendering for World of Warcraft, from outside the game process",
     "Нейронный рендеринг для World of Warcraft, снаружи процесса игры"},
    {"Stop overlay", "Остановить оверлей"},
    {"Start overlay", "Запустить оверлей"},

    {"Before you use this", "Прежде чем начать"},
    {"This sidecar never loads code into Wow.exe. That is what makes it safe, "
     "and it is checked automatically every time it is built.\n\n"
     "ReShade placed next to Wow.exe is a different thing entirely, and "
     "Blizzard bans accounts for it. This tool will refuse to install there and "
     "will refuse to run if it finds an injector in your WoW folder.\n\n"
     "No third-party tool can promise you will never be banned. What this one "
     "can promise is that it does not do any of the things Blizzard bans people "
     "for.",
     "Эта программа никогда не загружает код в Wow.exe. Именно это делает её "
     "безопасной, и это проверяется автоматически при каждой сборке.\n\n"
     "ReShade, положенный рядом с Wow.exe, — совсем другое дело, и за это "
     "Blizzard банит учётные записи. Этот инструмент откажется устанавливаться "
     "туда и откажется запускаться, если найдёт инжектор в вашей папке WoW.\n\n"
     "Ни один сторонний инструмент не может пообещать, что вас никогда не "
     "забанят. Этот может пообещать другое: он не делает ничего из того, за что "
     "Blizzard банит."},
    {"I understand", "Понятно"},

    // --------------------------------------------------------------- status
    {"Live", "Сейчас"},
    {"OVERLAY FPS", "КАДРЫ ОВЕРЛЕЯ"},
    {"LATENCY p50", "ЗАДЕРЖКА p50"},
    {"LATENCY p99", "ЗАДЕРЖКА p99"},
    {"CAPTURED FPS", "КАДРЫ ЗАХВАТА"},

    {"WoW: borderless", "WoW: без рамки"},
    {"WoW: not borderless", "WoW: не без рамки"},
    {"WoW: not running", "WoW: не запущен"},
    {"Overlay: running", "Оверлей: работает"},
    {"Overlay: stopped", "Оверлей: остановлен"},

    {"READY", "ГОТОВО"},
    {"CHECK", "ПРОВЕРЬТЕ"},
    {"BLOCKED", "БЛОКИРУЕТ"},

    {"Hide overlay (A/B compare)", "Скрыть оверлей (сравнение A/B)"},
    {"Show overlay", "Показать оверлей"},
    {"Hide HUD", "Скрыть HUD"},
    {"Show HUD", "Показать HUD"},
    {"Hiding the overlay uncovers the untouched game without stopping capture.",
     "Скрытие оверлея открывает нетронутую игру, не останавливая захват."},

    {"GPU MEMORY USED BY THE SIDECAR", "ПАМЯТЬ GPU, ЗАНЯТАЯ ПРОГРАММОЙ"},
    {"  --  SPILLING", "  —  ВЫТЕСНЯЕТСЯ"},
    {"This is the sidecar's own share, not the whole card. More of it would not "
     "be faster -- the pass allocates what it needs. The number that matters is "
     "whether any has spilled.",
     "Это доля самой программы, а не вся карта. Больше — не быстрее: проход "
     "выделяет столько, сколько ему нужно. Значение имеет другое — вытеснено "
     "ли что-нибудь."},
    {"WHERE THE FRAME GOES", "НА ЧТО УХОДИТ КАДР"},
    {"Frames", "Кадры"},
    {"%llu presented, %llu dropped", "%llu показано, %llu отброшено"},
    {"Pass", "Проход"},
    {"Runtime", "Рантайм"},
    {"Verdict", "Вердикт"},
    {"playable", "играбельно"},
    {"marginal", "на грани"},
    {"too slow", "слишком медленно"},
    {"The overlay is starting. Numbers appear after the first few frames.",
     "Оверлей запускается. Числа появятся после первых кадров."},
    {"Nothing running.", "Ничего не запущено."},

    {"The card is full, so the driver is moving resources to system memory and "
     "every frame waits on the PCIe bus. It shows below as a huge GPU wait with "
     "the GPU nearly idle, which is easy to mistake for a slow neural pass.\n\n"
     "Close what else is using the card -- browsers, streaming and capture "
     "tools, anything compositing a second monitor -- and lower the game's "
     "texture quality. Nothing in this tool can make room.",
     "Карта заполнена, поэтому драйвер перемещает ресурсы в системную память, и "
     "каждый кадр ждёт шину PCIe. Ниже это выглядит как огромное ожидание GPU "
     "при почти простаивающем GPU — легко принять за медленный нейронный "
     "проход.\n\n"
     "Закройте всё остальное, что нагружает карту: браузеры, стриминг и "
     "захват, любую композицию второго монитора, — и снизьте качество текстур в "
     "игре. Освободить память эта программа не может."},
    {"This is the desktop compositor's rate, not the sidecar's and not the "
     "game's -- the game may well be running far faster. It is usually set by "
     "your monitor's refresh rate and, on a multi-monitor setup with mismatched "
     "refresh rates, by the slowest one. Nothing in this tool can raise it.",
     "Это частота композитора рабочего стола, а не программы и не игры — игра "
     "вполне может идти намного быстрее. Обычно её задаёт частота обновления "
     "монитора, а при нескольких мониторах с разной частотой — самый медленный "
     "из них. Поднять её эта программа не может."},
    {"The game is competing for the GPU. Measured on the development machine, "
     "the same scene ran at 11.7 fps with the game focused and 35.2 fps with it "
     "in the background, purely because an unfocused game throttles itself and "
     "hands the card back.\n\n"
     "Cap the game's frame rate. This costs nothing: the overlay can never show "
     "more than the capture rate above, so every frame the game renders beyond "
     "that is discarded before it reaches here.",
     "Игра соперничает за GPU. На машине разработчика одна и та же сцена шла "
     "при 11,7 кадра/с с игрой в фокусе и при 35,2 кадра/с с игрой в фоне — "
     "просто потому, что игра без фокуса сама себя придерживает и отдаёт "
     "карту.\n\n"
     "Ограничьте частоту кадров в игре. Это ничего не стоит: оверлей всё равно "
     "не покажет больше, чем частота захвата выше, поэтому каждый кадр сверх "
     "неё отбрасывается, не доходя сюда."},
    {"The overlay is presenting close to every frame Windows hands it, and "
     "there is memory to spare. Nothing to fix here.",
     "Оверлей показывает почти каждый кадр, который отдаёт Windows, и памяти "
     "хватает. Здесь чинить нечего."},

    {"Some checks are failing. Open Checks to see what.",
     "Часть проверок не проходит. Откройте «Проверки», чтобы увидеть какие."},
    {"Files are missing. Open Setup to install them.",
     "Не хватает файлов. Откройте «Установку», чтобы их поставить."},
    {"Start World of Warcraft in borderless windowed mode, then press "
     "Start overlay.",
     "Запустите World of Warcraft в оконном режиме без рамки, затем нажмите "
     "«Запустить оверлей»."},
    {"Press Start overlay.", "Нажмите «Запустить оверлей»."},

    {"Playing with the overlay up", "Игра с поднятым оверлеем"},
    {"The overlay covers the game completely and passes every click straight "
     "through to it, so play normally. It never takes focus, so whatever had "
     "the keyboard keeps it -- if the game is not responding, alt-tab to World "
     "of Warcraft once.\n\n"
     "Ctrl+Alt+Backspace takes the overlay down from anywhere, without needing "
     "this window.",
     "Оверлей полностью закрывает игру и пропускает сквозь себя каждый клик, "
     "так что играйте как обычно. Он никогда не забирает фокус, поэтому "
     "клавиатура остаётся у того, у кого была: если игра не отвечает, один раз "
     "переключитесь на неё через Alt+Tab.\n\n"
     "Ctrl+Alt+Backspace снимает оверлей откуда угодно, без этого окна."},

    // ---------------------------------------------------------------- setup
    {"Required files", "Необходимые файлы"},
    {"Three files have to sit next to the sidecar. None of them is ours to "
     "redistribute and nothing here downloads anything -- neither binary in "
     "this project can reach the network at all. Fetch them yourself, then "
     "point this at them.",
     "Рядом с программой должны лежать три файла. Ни один из них мы не вправе "
     "распространять, и здесь ничего не скачивается: ни один из двух "
     "исполняемых файлов проекта вообще не умеет выходить в сеть. Возьмите их "
     "сами, затем укажите на них здесь."},
    {"INSTALLED", "УСТАНОВЛЕН"},
    {"MISSING", "ОТСУТСТВУЕТ"},
    {"OPTIONAL", "НЕОБЯЗАТЕЛЕН"},
    {"Wanted as %s   |   Source: %s", "Нужен как %s   |   Источник: %s"},
    {"Replace...", "Заменить…"},
    {"Choose file...", "Выбрать файл…"},

    {"Remove", "Удаление"},
    {"Deletes the files listed above from this folder, and nothing else. It "
     "never touches a WoW installation. The sidecar's own two executables stay; "
     "delete the folder to be rid of them.",
     "Удаляет из этой папки перечисленные выше файлы и ничего больше. "
     "Установленной игры это не касается никогда. Два собственных исполняемых "
     "файла программы остаются: чтобы избавиться и от них, удалите папку."},
    {"Also remove settings and logs", "Удалить также настройки и журналы"},
    {"Yes, remove them", "Да, удалить"},
    {"Remove installed files", "Удалить установленные файлы"},
    {"Stop the overlay first -- the files are in use.",
     "Сначала остановите оверлей — файлы заняты."},
    {"%zu file(s) would go.", "Будет удалено файлов: %zu."},

    {"That file no longer exists.", "Этого файла больше нет."},
    {"That is not a file.", "Это не файл."},
    {"Already installed.", "Уже установлен."},
    {"Could not copy it in: ", "Не удалось скопировать: "},
    {"Installed ", "Установлен "},
    {"Could not remove ", "Не удалось удалить "},
    {". Stop the overlay and close anything using it, then try again.",
     ". Остановите оверлей, закройте всё, что его использует, и повторите."},
    {"Removed ", "Удалено файлов: "},
    {" file(s).", "."},

    // The installer's component descriptions, which the setup page draws
    // through the same helpers as everything else.
    {"DLSS 5 neural rendering runtime", "Рантайм нейронного рендеринга DLSS 5"},
    {"The neural network itself. On an RTX 40 card this must be a build "
     "patched for Ada; the stock runtime is Blackwell-only and fails at "
     "feature creation with no diagnostic.",
     "Сама нейросеть. На карте RTX 40 нужна сборка, пропатченная под Ada: "
     "штатный рантайм работает только на Blackwell и падает при создании "
     "функции без всякой диагностики."},
    {"ReShade, with add-on support", "ReShade с поддержкой дополнений"},
    {"Hosts the add-on inside the sidecar's own process. The add-on-enabled "
     "build is required; the plain one loads no add-ons at all.",
     "Размещает дополнение внутри собственного процесса программы. Нужна "
     "сборка с поддержкой дополнений: обычная не загружает их вовсе."},
    {"RenoDX DLSS 5 add-on", "Дополнение RenoDX DLSS 5"},
    {"Detours the sidecar's own NGX calls and substitutes neural-rendered "
     "output. This is what makes the pass neural rather than a plain DLAA.",
     "Перехватывает собственные вызовы NGX программы и подставляет результат "
     "нейронного рендеринга. Именно это делает проход нейронным, а не обычным "
     "DLAA."},
    {"DLSS upscaling runtime", "Рантайм масштабирования DLSS"},
    {"Optional. Only consulted if the add-on's work-in-progress upscaling "
     "path is switched on; neural rendering itself does not need it.",
     "Необязателен. Нужен только если включён незавершённый путь "
     "масштабирования дополнения; самому нейронному рендерингу он не нужен."},

    // --------------------------------------------------------------- checks
    {"System checks", "Проверки системы"},
    {"Nothing here opens, reads, writes or hooks the game process. The import "
     "table of every binary is checked against that claim at build time.",
     "Ничто здесь не открывает, не читает, не пишет и не перехватывает процесс "
     "игры. Таблица импорта каждого исполняемого файла сверяется с этим "
     "утверждением при сборке."},
    {"WoW folder (used only to scan filenames for injectors)",
     "Папка WoW (нужна только для проверки имён файлов на инжекторы)"},
    {"Browse...", "Обзор…"},
    {"Detect", "Найти"},
    {"Re-run checks", "Проверить снова"},
    {"Detected from Battle.net's own uninstall entry. Point this at the folder "
     "Wow.exe sits in, not its parent: an injector has to be next to the "
     "executable to be loaded by it. Nothing inside is opened -- only the file "
     "names are read.",
     "Определяется по записи об удалении, которую создаёт Battle.net. Укажите "
     "папку, в которой лежит Wow.exe, а не родительскую: инжектор должен "
     "находиться рядом с исполняемым файлом, чтобы тот его загрузил. Ничто "
     "внутри не открывается — читаются только имена файлов."},

    {"Graphics adapter", "Видеоадаптер"},
    {"No NVIDIA adapter found.", "Адаптер NVIDIA не найден."},
    {"This sidecar needs an NVIDIA RTX 40 or RTX 50 card.",
     "Программе нужна карта NVIDIA RTX 40 или RTX 50."},
    {"RTX 40 (Ada) or RTX 50 (Blackwell) is required. Older cards are refused "
     "rather than run badly.",
     "Требуется RTX 40 (Ada) или RTX 50 (Blackwell). Более старым картам "
     "отказано, вместо того чтобы работать плохо."},
    {"Display driver", "Драйвер дисплея"},
    {"No NVIDIA adapter to query.", "Нет адаптера NVIDIA для опроса."},
    {"Install an NVIDIA RTX 40 or RTX 50 card.",
     "Установите карту NVIDIA RTX 40 или RTX 50."},
    {"User-mode driver ", "Драйвер пользовательского режима "},
    {"Could not read the driver version.", "Не удалось прочитать версию драйвера."},
    {"Not fatal. Update to a current NVIDIA driver if capture misbehaves.",
     "Не критично. Обновите драйвер NVIDIA, если захват ведёт себя странно."},
    {"Windows version", "Версия Windows"},
    {"Could not read the Windows build number.",
     "Не удалось прочитать номер сборки Windows."},
    {"Not fatal, but this project is only supported on Windows 11.",
     "Не критично, но проект поддерживается только на Windows 11."},
    {"Build ", "Сборка "},
    {"Windows 11 is required: the overlay depends on compositor behaviour that "
     "Windows 10 does not provide.",
     "Требуется Windows 11: оверлей опирается на поведение композитора, "
     "которого нет в Windows 10."},
    {"Display refresh rate", "Частота обновления дисплея"},
    {"Could not read the current display mode.",
     "Не удалось прочитать текущий режим дисплея."},
    {"Not fatal. Check your monitor settings if pacing looks wrong.",
     "Не критично. Проверьте настройки монитора, если темп кадров выглядит "
     "неверным."},
    {"Below 120 Hz the overlay's added latency is a larger share of the frame. "
     "It will still run.",
     "Ниже 120 Гц добавленная оверлеем задержка занимает большую долю кадра. "
     "Работать он всё равно будет."},
    {"Neural runtime", "Нейронный рантайм"},
    {"nvngx_dlssnr.dll not found next to the sidecar.",
     "nvngx_dlssnr.dll не найден рядом с программой."},
    {"Optional until the neural pass ships. Without it the pipeline runs the "
     "passthrough pass.",
     "Необязателен, пока нейронный проход не выпущен. Без него конвейер "
     "выполняет сквозной проход."},
    {"Check the file is not locked by another process, and that the sidecar has "
     "permission to read it.",
     "Проверьте, что файл не занят другим процессом и что у программы есть "
     "право его читать."},
    {"Supply a runtime built for this GPU, or the neural pass will fall back to "
     "passthrough.",
     "Поставьте рантайм, собранный под эту карту, иначе нейронный проход "
     "откатится к сквозному."},
    {"The pass will still try this build. If it fails, quote the SHA-256 above "
     "when reporting it.",
     "Проход всё равно попробует эту сборку. Если не выйдет, укажите SHA-256 "
     "выше в сообщении об ошибке."},
    {"ReShade host (optional)", "Хост ReShade (необязательно)"},
    {"No ReShade host alongside the sidecar.",
     "Рядом с программой нет хоста ReShade."},
    {"Only needed for the ReShade-hosted neural pass. Install ReShade against "
     "the sidecar so it lands as dxgi.dll. Never place ReShade next to Wow.exe.",
     "Нужен только для нейронного прохода, размещённого в ReShade. Установите "
     "ReShade на эту программу, чтобы он лёг как dxgi.dll. Никогда не кладите "
     "ReShade рядом с Wow.exe."},
    {"World of Warcraft window", "Окно World of Warcraft"},
    {"WoW is not running.", "WoW не запущен."},
    {"Start the game, then run the probes again.",
     "Запустите игру и повторите проверки."},
    {" borderless windowed", " оконный без рамки"},
    {" windowed with a border, or exclusive fullscreen",
     " оконный с рамкой либо эксклюзивный полноэкранный"},
    {"Set WoW to borderless windowed. Exclusive fullscreen has no compositor "
     "surface to capture and yields black frames.",
     "Переведите WoW в оконный режим без рамки. У эксклюзивного полноэкранного "
     "нет поверхности композитора для захвата, и кадры выходят чёрными."},
    {"Injector scan of the WoW folder", "Проверка папки WoW на инжекторы"},
    {"No WoW folder set, so nothing was scanned.",
     "Папка WoW не задана, поэтому ничего не проверено."},
    {"Point the manager at your WoW folder so it can check for injectors before "
     "launching.",
     "Укажите менеджеру папку WoW, чтобы он мог проверить наличие инжекторов "
     "перед запуском."},
    {"No injector loaders found.", "Загрузчиков-инжекторов не найдено."},
    {"Found: ", "Найдено: "},
    {"Remove these from your WoW folder. Blizzard bans accounts for in-process "
     "injectors, and this tool refuses to run alongside one.",
     "Удалите это из папки WoW. Blizzard банит учётные записи за инжекторы "
     "внутри процесса, и этот инструмент отказывается работать рядом с таким."},
    {"Sidecar install location", "Расположение программы"},
    {"Move the sidecar outside your WoW folder. Anything sitting next to "
     "Wow.exe looks like an injector, which is the one thing this design exists "
     "to avoid.",
     "Перенесите программу за пределы папки WoW. Всё, что лежит рядом с "
     "Wow.exe, выглядит как инжектор, а именно этого вся конструкция и "
     "избегает."},

    // ------------------------------------------------------- runtime verdicts
    {" is the ", " — это "},
    {" runtime, version ", ", рантайм версии "},
    {" could not be read, so it has no digest.",
     " не удалось прочитать, поэтому у него нет отпечатка."},
    {" is not a build this manifest knows. Its SHA-256 is ",
     " — сборка, не известная этому манифесту. Её SHA-256: "},
    {" -- quote that when reporting it.", " — укажите его в сообщении об ошибке."},
    {"This GPU (", "Эта видеокарта ("},
    {") is outside the supported matrix; neural rendering needs Ada or "
     "Blackwell.",
     ") вне поддерживаемой матрицы; нейронному рендерингу нужны Ada или "
     "Blackwell."},
    {"This is the stock runtime, which is built for Blackwell. On an Ada card "
     "neural-rendering feature creation fails with no explanation. An "
     "Ada-compatible build of nvngx_dlssnr.dll is required.",
     "Это штатный рантайм, собранный под Blackwell. На карте Ada создание "
     "функции нейронного рендеринга падает без всяких объяснений. Нужна сборка "
     "nvngx_dlssnr.dll, совместимая с Ada."},
    {"The ", "Рантайм "},
    {" runtime does not run on ", " не работает на "},

    // --------------------------------------------------------------- tuning
    {"Choose a look", "Выберите вид"},
    {"Pick one. Every setting below is chosen for you, and the combinations "
     "here are ones that have actually been run -- unlike most of the "
     "arrangements you can reach by moving sliders individually.",
     "Выберите один. Все настройки ниже подобраны за вас, и эти сочетания "
     "действительно запускались — в отличие от большинства комбинаций, к "
     "которым можно прийти, двигая ползунки поодиночке."},

    {"Recommended", "Рекомендуемый"},
    {"The tuned default. Start here.", "Подобранный вариант. Начните с него."},
    {"Full neural intensity with the CNN F render preset, which clamps temporal "
     "history hard -- the right choice when motion vectors are estimated from "
     "colour rather than rendered by the game.",
     "Полная интенсивность нейросети с пресетом рендеринга CNN F, который "
     "жёстко ограничивает временную историю, — верный выбор, когда векторы "
     "движения оцениваются по цвету, а не выдаются игрой."},
    {"Softer", "Мягче"},
    {"Half strength. Use if the picture looks over-processed.",
     "Половина силы. Если картинка выглядит переобработанной."},
    {"The same pipeline with the neural result mixed in at 60%. Cheaper on the "
     "eyes for interface-heavy scenes, and the first thing to try if faces or "
     "text look waxy.",
     "Тот же конвейер, но результат нейросети подмешан на 60%. Легче для глаз "
     "на сценах с обилием интерфейса и первое, что стоит попробовать, если лица "
     "или текст выглядят восковыми."},
    {"Most stable", "Самый стабильный"},
    {"For smearing, or flicker on flames and lights.",
     "От смазывания и мерцания на огне и источниках света."},
    {"Switches to CNN E, which clamps temporal history hardest, and eases the "
     "intensity. This is the preset for when motion looks smeared -- the "
     "estimated motion vectors are being confidently wrong and this contains "
     "them.",
     "Переключается на CNN E, который сильнее всех ограничивает временную "
     "историю, и снижает интенсивность. Этот пресет — для смазанного движения: "
     "оценённые векторы уверенно ошибаются, а он их сдерживает."},
    {"Off (A/B baseline)", "Выключено (базовый вариант для A/B)"},
    {"Capture and present, untouched.", "Захват и вывод, без обработки."},
    {"No neural work at all, on the same capture and present path. This is the "
     "honest comparison: whatever you see here is what the overlay costs you "
     "before any neural rendering happens.",
     "Никакой работы нейросети, тот же путь захвата и вывода. Это честное "
     "сравнение: то, что вы видите здесь, — цена самого оверлея до всякого "
     "нейронного рендеринга."},

    {"Save settings", "Сохранить настройки"},
    {"Unsaved changes.", "Есть несохранённые изменения."},
    {"Restart the overlay to apply.", "Перезапустите оверлей, чтобы применить."},
    {"Every individual setting", "Все настройки по отдельности"},
    {"Nothing in here is needed for normal use.",
     "Ничего отсюда для обычной работы не нужно."},
    {"Pipeline", "Конвейер"},
    {"These are written to sidecar.toml, and the neural ones are projected into "
     "ReShade.ini where the add-on reads them. The add-on reads that file once, "
     "when it loads, so a change reaches the picture at the overlay's next "
     "start -- not while it runs.",
     "Они пишутся в sidecar.toml, а нейронные проецируются в ReShade.ini, "
     "откуда их читает дополнение. Дополнение читает этот файл один раз, при "
     "загрузке, поэтому изменение дойдёт до картинки при следующем запуске "
     "оверлея, а не во время работы."},
    {"Neural pass", "Нейронный проход"},
    {"passthrough -- capture and present, untouched",
     "сквозной — захват и вывод, без обработки"},
    {"reshade -- DLSS 5 neural rendering",
     "reshade — нейронный рендеринг DLSS 5"},
    {"Passthrough is the honest A/B baseline: the same capture and present path "
     "with no neural work in it at all.",
     "Сквозной проход — честная база для сравнения A/B: тот же путь захвата и "
     "вывода, но без всякой работы нейросети."},
    {"DLSS preset", "Пресет DLSS"},
    {"Optical flow grid", "Сетка оптического потока"},
    {"1 -- finest, most expensive", "1 — самая мелкая, самая дорогая"},
    {"4 -- default", "4 — по умолчанию"},
    {"Pixels per estimated motion vector. Finer costs more and is not obviously "
     "better: the vectors are estimated from colour, so a finer grid can "
     "sharpen the estimate or the error equally.",
     "Пикселей на один оценённый вектор движения. Мельче — дороже и не "
     "очевидно лучше: векторы оцениваются по цвету, поэтому мелкая сетка "
     "одинаково уточняет и оценку, и ошибку."},
    {"Synthetic depth", "Синтетическая глубина"},
    {"There is no real depth buffer to capture out of the composited frame, so "
     "a constant plane is bound instead. It costs temporal stability under "
     "motion rather than preventing NR from running; this dial is worth a try "
     "if motion smears.",
     "Из составленного кадра нельзя захватить настоящий буфер глубины, поэтому "
     "вместо него привязывается постоянная плоскость. Это стоит временной "
     "стабильности в движении, но не мешает нейронному рендерингу работать; "
     "покрутить стоит, если движение смазывается."},
    {"Neural rendering strength", "Сила нейронного рендеринга"},
    {"Passed straight through to the RenoDX add-on. The add-on owns what these "
     "mean; we only carry them.",
     "Передаётся напрямую дополнению RenoDX. Смысл этих значений определяет "
     "дополнение; мы их только переносим."},
    {"Intensity", "Интенсивность"},
    {"How much of the neural result is mixed in. Start here.",
     "Насколько сильно подмешивается результат нейросети. Начните отсюда."},
    {"Add-on preset", "Пресет дополнения"},
    {"Style", "Стиль"},
    {"Colour strength", "Сила цвета"},
    {"At zero the add-on's colour handling collapses to black, so this is not a "
     "subtle dial.",
     "На нуле обработка цвета дополнением схлопывается в чёрное, так что "
     "регулятор совсем не деликатный."},
    {"HDR transfer strength", "Сила передаточной кривой HDR"},
    {"Paper-white scale", "Масштаб белой бумаги"},
    {"Both only matter on an HDR display. The sidecar captures SDR today, so "
     "leave them at 1.00 unless you are experimenting.",
     "Оба имеют значение только на HDR-дисплее. Сейчас программа захватывает "
     "SDR, поэтому оставьте 1.00, если не экспериментируете."},
    {"These three have no documented default, so they are written only once you "
     "move them. Clearing the box returns them to untouched, which leaves "
     "whatever the add-on does by itself.",
     "У этих трёх нет задокументированного значения по умолчанию, поэтому они "
     "записываются, только когда вы их подвинете. Снятие флажка возвращает их в "
     "нетронутое состояние — дополнение делает то, что делает само."},
    {"Enable the add-on's upscaling (work in progress)",
     "Включить масштабирование дополнения (в разработке)"},
    {"The add-on marks this unfinished, and the sidecar feeds it a "
     "native-resolution DLAA contract, so it usually reports that it fell back "
     "to native. Off is the tested path.",
     "Дополнение помечает это как незавершённое, а программа передаёт ему "
     "контракт DLAA в родном разрешении, поэтому обычно оно сообщает об откате "
     "к родному. Проверенный путь — выключено."},
    {"Hook mode", "Режим перехвата"},
    {"0 turns neural rendering off entirely, 1 adds Streamline hooks, 2 is NGX "
     "only. Two is what this sidecar wants: it makes the NGX calls itself and "
     "there is no Streamline in the process.",
     "0 полностью выключает нейронный рендеринг, 1 добавляет перехваты "
     "Streamline, 2 — только NGX. Программе нужна двойка: она сама делает "
     "вызовы NGX, и Streamline в процессе нет."},
    {"Overlay", "Оверлей"},
    {"Show the HUD", "Показывать HUD"},
    {"Show the overlay on start", "Показывать оверлей при запуске"},
    {"Reset to defaults", "Сбросить к значениям по умолчанию"},

    // ------------------------------------------------------------------ log
    {"%llu earlier line(s) dropped", "Отброшено предыдущих строк: %llu"},

    // -------------------------------------------------------- runtime dialogs
    {"Window class not found.", "Класс окна не найден."},
    {"World of Warcraft is not running.", "World of Warcraft не запущен."},
    {"World of Warcraft must run in borderless windowed mode.\n"
     "Exclusive fullscreen has no compositor surface to capture.",
     "World of Warcraft должен работать в оконном режиме без рамки.\n"
     "У эксклюзивного полноэкранного нет поверхности композитора для захвата."},
    {"%hs is not supported. RTX 40 or RTX 50 required.",
     "%hs не поддерживается. Требуется RTX 40 или RTX 50."},
    {"Failed to create the pipeline.", "Не удалось создать конвейер."},
    {"An overlay is already running.\n"
     "Stop it from the manager before starting another.",
     "Оверлей уже запущен.\n"
     "Остановите его из менеджера, прежде чем запускать другой."},
    {"The graphics device was reset and could not be rebuilt.",
     "Графическое устройство было сброшено и не может быть пересоздано."},
};

const size_t kRussianTableSize = sizeof(kRussianTable) / sizeof(kRussianTable[0]);

}  // namespace sidecar
