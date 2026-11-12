// The mod's text in the game's language, as FH2 ANNA does it (its src/lang.cpp).
//
// The language: the string tables' manager ([game+0x3662990]) has the text language's code, an
// MSVC std::string ("EN", "DE", "CHT"...), at +0x30 (as Extra Options reads it). The rows go to
// the platform at fh2mod_init, before the game has a language, so the one seen last is kept in
// the ini ([Language] Last) for them; the tile's title is read each time the page is.
// Czech and Hungarian stay English, as the game's own text is there (see english_there).
// The key names ("Space", "LShift"...) stay as they are: they're the ini's.
#pragma once
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

namespace lang {

constexpr uintptr_t RVA_STRINGS = 0x3662990;

// The languages, in the table's order (English is the key itself; GB is English).
inline const char* const kCodes[] = {"BR", "DE", "FR", "ES", "MX", "IT", "JP", "KO", "CHT", "CZ", "HU", "PL", "RU", "NL"};
constexpr int kLangs = sizeof kCodes / sizeof kCodes[0];

struct Text {
    const char* en;
    const char* t[kLangs];
};

// clang-format off
inline const Text kTexts[] = {
// the tile and the mod
{"KEYBOARD & MOUSE", {"TECLADO E MOUSE", "TASTATUR & MAUS", "CLAVIER ET SOURIS", "TECLADO Y RATÓN", "TECLADO Y MOUSE", "TASTIERA E MOUSE", "キー＆マウス", "키보드와 마우스", "鍵盤滑鼠", "KLÁVESNICE A MYŠ", "BILLENTYŰZET ÉS EGÉR", "KLAWIATURA I MYSZ", "КЛАВИАТУРА И МЫШЬ", "TOETSENBORD EN MUIS"}},
{"Drive with the keyboard, point at things in the menus and click them, drag the map, turn the garage camera, place decals with the keyboard, and fly the photo mode camera, all without picking up a controller.",
 {"Dirija com o teclado, aponte e clique nos menus, arraste o mapa, gire a câmera da garagem, posicione adesivos com o teclado e voe com a câmera do modo foto, tudo sem pegar no controle.",
  "Fahre mit der Tastatur, zeige und klicke in den Menüs, ziehe die Karte, drehe die Garagenkamera, platziere Aufkleber mit der Tastatur und fliege die Fotomodus-Kamera, ganz ohne Controller.",
  "Conduisez au clavier, pointez et cliquez dans les menus, faites glisser la carte, tournez la caméra du garage, placez des stickers au clavier et pilotez la caméra du mode photo, sans jamais prendre la manette.",
  "Conduce con el teclado, señala y haz clic en los menús, arrastra el mapa, gira la cámara del garaje, coloca vinilos con el teclado y vuela con la cámara del modo foto, todo sin tocar el mando.",
  "Maneja con el teclado, señala y haz clic en los menús, arrastra el mapa, gira la cámara del garage, coloca vinilos con el teclado y vuela con la cámara del modo foto, todo sin tocar el control.",
  "Guida con la tastiera, punta e clicca nei menu, trascina la mappa, ruota la telecamera del garage, applica decalcomanie con la tastiera e fai volare la telecamera della modalità foto, tutto senza prendere il controller.",
  "キーボードで運転し、メニューはマウスで指してクリック、マップのドラッグ、ガレージのカメラの回転、キーボードでのデカール配置、フォトモードのカメラ操作まで、コントローラーなしでできます。",
  "키보드로 운전하고, 메뉴를 가리켜 클릭하고, 지도를 드래그하고, 차고 카메라를 돌리고, 키보드로 데칼을 배치하고, 사진 모드 카메라를 조종하세요. 모두 컨트롤러 없이 가능합니다.",
  "用鍵盤駕駛、在選單中指向並點擊、拖動地圖、旋轉車庫鏡頭、用鍵盤放置貼花，以及操控相片模式的鏡頭，全都不必拿起控制器。",
  "Řiďte klávesnicí, ukazujte a klikejte v nabídkách, přetahujte mapu, otáčejte kamerou v garáži, umisťujte polepy klávesnicí a létejte kamerou fotorežimu, to vše bez ovladače.",
  "Vezess billentyűzettel, mutass rá és kattints a menükben, húzd a térképet, forgasd a garázs kameráját, helyezz el matricákat billentyűzettel, és irányítsd a fotómód kameráját, mindezt kontroller nélkül.",
  "Jedź na klawiaturze, wskazuj i klikaj w menu, przeciągaj mapę, obracaj kamerę w garażu, umieszczaj naklejki klawiaturą i steruj kamerą trybu fotograficznego, a wszystko bez kontrolera.",
  "Ездите на клавиатуре, наводите и щёлкайте в меню, перетаскивайте карту, вращайте камеру в гараже, размещайте наклейки с клавиатуры и управляйте камерой фоторежима, и всё это без контроллера.",
  "Rijd met het toetsenbord, wijs en klik in de menu's, sleep de kaart, draai de garagecamera, plaats stickers met het toetsenbord en vlieg met de camera van de fotomodus, allemaal zonder controller."}},
// the rows
{"Keyboard controls", {"Controles do teclado", "Tastatursteuerung", "Commandes clavier", "Controles de teclado", "Controles de teclado", "Comandi da tastiera", "キーボード操作", "키보드 조작", "鍵盤操作", "Ovládání klávesnicí", "Billentyűzetes irányítás", "Sterowanie klawiaturą", "Управление с клавиатуры", "Toetsenbordbediening"}},
{"Drive with the keyboard and move around the menus with it. The rows below set the keys.",
 {"Dirija com o teclado e navegue pelos menus com ele. As linhas abaixo definem as teclas.",
  "Fahre mit der Tastatur und bewege dich damit durch die Menüs. Die Zeilen darunter legen die Tasten fest.",
  "Conduisez au clavier et naviguez dans les menus avec. Les lignes ci-dessous définissent les touches.",
  "Conduce con el teclado y muévete por los menús con él. Las filas de abajo asignan las teclas.",
  "Maneja con el teclado y muévete por los menús con él. Las filas de abajo asignan las teclas.",
  "Guida con la tastiera e usala per muoverti nei menu. Le righe qui sotto impostano i tasti.",
  "キーボードで運転し、メニューも操作します。下の項目でキーを設定します。",
  "키보드로 운전하고 메뉴를 이동합니다. 아래 항목에서 키를 설정합니다.",
  "用鍵盤駕駛並操作選單。下方各列可設定按鍵。",
  "Řiďte klávesnicí a pohybujte se jí i v nabídkách. Řádky níže nastavují klávesy.",
  "Vezess billentyűzettel, és navigálj vele a menükben. Az alábbi sorok állítják be a billentyűket.",
  "Jedź na klawiaturze i poruszaj się nią po menu. Wiersze poniżej ustawiają klawisze.",
  "Управляйте машиной с клавиатуры и перемещайтесь ею по меню. Строки ниже задают клавиши.",
  "Rijd met het toetsenbord en navigeer er ook de menu's mee. De rijen hieronder stellen de toetsen in."}},
{"Keyboard icons", {"Ícones do teclado", "Tastatursymbole", "Icônes clavier", "Iconos de teclado", "Íconos de teclado", "Icone della tastiera", "キーボードのアイコン", "키보드 아이콘", "鍵盤圖示", "Ikony klávesnice", "Billentyűzetikonok", "Ikony klawiatury", "Значки клавиатуры", "Toetsenbordpictogrammen"}},
{"While you play with the keyboard, the help bar shows its keys instead of the controller's buttons. A button on the controller brings the buttons back.",
 {"Enquanto você joga com o teclado, a barra de ajuda mostra as teclas no lugar dos botões do controle. Apertar um botão do controle traz os botões de volta.",
  "Solange du mit der Tastatur spielst, zeigt die Hilfeleiste ihre Tasten statt der Controllertasten. Ein Druck auf eine Controllertaste holt sie zurück.",
  "Quand vous jouez au clavier, la barre d'aide affiche ses touches au lieu des boutons de la manette. Un bouton de la manette les fait revenir.",
  "Mientras juegas con el teclado, la barra de ayuda muestra sus teclas en lugar de los botones del mando. Pulsar un botón del mando los devuelve.",
  "Mientras juegas con el teclado, la barra de ayuda muestra sus teclas en lugar de los botones del control. Presionar un botón del control los regresa.",
  "Mentre giochi con la tastiera, la barra dei comandi mostra i suoi tasti al posto dei pulsanti del controller. Premere un pulsante del controller li fa tornare.",
  "キーボードでプレイ中は、ヘルプバーにコントローラーのボタンの代わりにキーを表示します。コントローラーのボタンを押すと元に戻ります。",
  "키보드로 플레이하는 동안 도움말 표시줄에 컨트롤러 버튼 대신 키를 표시합니다. 컨트롤러 버튼을 누르면 다시 버튼이 표시됩니다.",
  "用鍵盤遊玩時，說明列會顯示按鍵而非控制器按鈕。按下控制器按鈕即恢復顯示按鈕。",
  "Když hrajete na klávesnici, panel nápovědy ukazuje její klávesy místo tlačítek ovladače. Stisk tlačítka na ovladači vrátí tlačítka.",
  "Amíg billentyűzettel játszol, a súgósáv a billentyűket mutatja a kontroller gombjai helyett. A kontroller egy gombja visszahozza a gombokat.",
  "Gdy grasz na klawiaturze, pasek pomocy pokazuje jej klawisze zamiast przycisków kontrolera. Naciśnięcie przycisku na kontrolerze przywraca przyciski.",
  "Пока вы играете на клавиатуре, панель подсказок показывает её клавиши вместо кнопок контроллера. Нажатие кнопки контроллера возвращает кнопки.",
  "Terwijl je met het toetsenbord speelt, toont de hulpbalk de toetsen in plaats van de knoppen van de controller. Een knop op de controller brengt de knoppen terug."}},
{"Mouse hover selects",
 {"Selecionar ao passar o mouse",
  "Auswahl per Mauszeiger",
  "Sélection au survol",
  "Seleccionar al pasar el ratón",
  "Seleccionar al pasar el mouse",
  "Selezione al passaggio del mouse",
  "マウスオーバーで選択",
  "마우스를 올리면 선택",
  "滑鼠懸停即選取",
  "Výběr najetím myši",
  "Kijelölés rámutatással",
  "Zaznaczanie najechaniem myszą",
  "Выбор наведением мыши",
  "Selecteren door aanwijzen"}},
{"Moving the mouse over a button, list row or slider selects it. Off, only a click does.",
 {"Passar o mouse sobre um botão, linha de lista ou controle deslizante o seleciona. Desativado, só um clique seleciona.",
  "Fährt die Maus über eine Schaltfläche, Listenzeile oder einen Schieberegler, wird sie ausgewählt. Aus: Nur ein Klick wählt aus.",
  "Survoler un bouton, une ligne de liste ou un curseur avec la souris le sélectionne. Désactivé, seul un clic le fait.",
  "Pasar el ratón sobre un botón, una fila de lista o un control deslizante lo selecciona. Desactivado, solo lo hace un clic.",
  "Pasar el mouse sobre un botón, una fila de lista o un control deslizante lo selecciona. Desactivado, solo lo hace un clic.",
  "Passare il mouse su un pulsante, una riga di elenco o un cursore lo seleziona. Se disattivato, lo fa solo un clic.",
  "ボタン、リストの行、スライダーにマウスを重ねると選択します。オフにするとクリックでのみ選択します。",
  "버튼, 목록 행, 슬라이더 위로 마우스를 가져가면 선택됩니다. 끄면 클릭해야만 선택됩니다.",
  "滑鼠移到按鈕、清單列或滑桿上即選取。關閉時只有點擊才會選取。",
  "Najetí myší na tlačítko, řádek seznamu nebo posuvník jej vybere. Při vypnutí vybírá jen kliknutí.",
  "Ha az egeret egy gomb, listasor vagy csúszka fölé viszed, kijelöli. Kikapcsolva csak a kattintás jelöl ki.",
  "Najechanie myszą na przycisk, wiersz listy lub suwak zaznacza go. Po wyłączeniu zaznacza tylko kliknięcie.",
  "Наведение мыши на кнопку, строку списка или ползунок выбирает их. Если выключено, выбирает только щелчок.",
  "De muis over een knop, lijstregel of schuifregelaar bewegen selecteert die. Uit: alleen een klik doet dat."}},
{"Mouse free look", {"Visão livre com o mouse", "Freie Sicht mit der Maus", "Vue libre à la souris", "Vista libre con el ratón", "Vista libre con el mouse", "Visuale libera col mouse", "マウスで視点操作", "마우스 자유 시점", "滑鼠自由視角", "Volný pohled myší", "Szabad nézelődés egérrel", "Swobodne rozglądanie myszą", "Свободный обзор мышью", "Vrij rondkijken met muis"}},
{"While driving, moving the mouse looks around. The cursor stays in the game's window: Alt+Tab to leave it.",
 {"Ao dirigir, mover o mouse olha ao redor. O cursor fica na janela do jogo: use Alt+Tab para sair dela.",
  "Beim Fahren schaust du dich mit der Maus um. Der Mauszeiger bleibt im Spielfenster: Mit Alt+Tab verlässt du es.",
  "En conduisant, bouger la souris permet de regarder autour. Le curseur reste dans la fenêtre du jeu : Alt+Tab pour en sortir.",
  "Al conducir, mover el ratón mira alrededor. El cursor se queda en la ventana del juego: Alt+Tab para salir de ella.",
  "Al manejar, mover el mouse mira alrededor. El cursor se queda en la ventana del juego: Alt+Tab para salir de ella.",
  "Durante la guida, muovere il mouse fa guardare intorno. Il cursore resta nella finestra del gioco: Alt+Tab per uscirne.",
  "運転中、マウスを動かすと周りを見渡せます。カーソルはゲームのウィンドウ内にとどまります。離れるには Alt+Tab を押します。",
  "운전 중 마우스를 움직이면 주위를 둘러봅니다. 커서는 게임 창 안에 머무르며, 벗어나려면 Alt+Tab을 누르세요.",
  "駕駛時移動滑鼠即可查看四周。滑鼠指標會留在遊戲視窗內：按 Alt+Tab 離開。",
  "Při jízdě se pohybem myši rozhlížíte. Kurzor zůstává v okně hry: opustíte ho klávesami Alt+Tab.",
  "Vezetés közben az egér mozgatásával körülnézhetsz. A kurzor a játék ablakában marad: Alt+Tab-bal hagyhatod el.",
  "Podczas jazdy ruch myszą pozwala się rozglądać. Kursor zostaje w oknie gry: Alt+Tab, aby je opuścić.",
  "Во время езды движение мыши позволяет осматриваться. Курсор остаётся в окне игры: Alt+Tab, чтобы выйти из него.",
  "Tijdens het rijden kijk je rond door de muis te bewegen. De cursor blijft in het spelvenster: Alt+Tab om het te verlaten."}},
{"Mouse free look sensitivity",
 {"Sensibilidade da visão livre",
  "Empfindlichkeit der freien Sicht",
  "Sensibilité de la vue libre",
  "Sensibilidad de la vista libre",
  "Sensibilidad de la vista libre",
  "Sensibilità della visuale libera",
  "マウス視点操作の感度",
  "마우스 자유 시점 감도",
  "滑鼠自由視角靈敏度",
  "Citlivost volného pohledu",
  "Egeres nézelődés érzékenysége",
  "Czułość rozglądania myszą",
  "Чувствительность обзора мышью",
  "Gevoeligheid rondkijken"}},
{"How far the camera turns for the mouse's movement while you look around. 50 is the usual, 100 twice as far, 0 not at all.",
 {"Quanto a câmera gira com o movimento do mouse ao olhar ao redor. 50 é o normal, 100 o dobro, 0 nada.",
  "Wie weit sich die Kamera bei Mausbewegung dreht, während du dich umschaust. 50 ist normal, 100 doppelt so weit, 0 gar nicht.",
  "De combien la caméra tourne selon le mouvement de la souris quand vous regardez autour. 50 est la normale, 100 deux fois plus, 0 pas du tout.",
  "Cuánto gira la cámara con el movimiento del ratón al mirar alrededor. 50 es lo normal, 100 el doble y 0 nada.",
  "Cuánto gira la cámara con el movimiento del mouse al mirar alrededor. 50 es lo normal, 100 el doble y 0 nada.",
  "Quanto ruota la telecamera col movimento del mouse mentre ti guardi intorno. 50 è il normale, 100 il doppio, 0 per niente.",
  "見渡すときに、マウスの動きに対してカメラがどれだけ回るかです。50 が標準、100 で 2 倍、0 で回りません。",
  "주위를 둘러볼 때 마우스 움직임에 따라 카메라가 도는 정도입니다. 50이 기본, 100은 두 배, 0은 돌지 않습니다.",
  "查看四周時，鏡頭隨滑鼠移動轉動的幅度。50 為一般，100 為兩倍，0 則不轉動。",
  "Jak moc se kamera při rozhlížení otočí podle pohybu myši. 50 je obvyklé, 100 dvojnásobek, 0 vůbec.",
  "Mennyit fordul a kamera az egér mozgására körülnézés közben. 50 a szokásos, 100 a kétszerese, 0 egyáltalán nem.",
  "Jak mocno kamera obraca się wraz z ruchem myszy podczas rozglądania. 50 to norma, 100 dwa razy mocniej, 0 wcale.",
  "Насколько поворачивается камера от движения мыши при осмотре. 50 обычно, 100 вдвое сильнее, 0 совсем нет.",
  "Hoe ver de camera draait bij muisbeweging terwijl je rondkijkt. 50 is normaal, 100 twee keer zo ver, 0 helemaal niet."}},
{"Keyboard preset", {"Predefinição do teclado", "Tastenbelegung", "Préréglage clavier", "Preajuste de teclado", "Preajuste de teclado", "Preimpostazione tastiera", "キー配置のプリセット", "키보드 프리셋", "鍵盤預設配置", "Předvolba klávesnice", "Billentyűkiosztás", "Układ klawiszy", "Раскладка клавиш", "Toetsenbordindeling"}},
{"Sets every key below at once. WS drive, arrow steer is the mod's own layout; AZERTY and QWERTZ put the keys where WASD's are on those keyboards. Changing a key makes it Custom.",
 {"Define todas as teclas abaixo de uma vez. WS para dirigir, setas para virar é o layout do próprio mod; AZERTY e QWERTZ colocam as teclas onde ficam as do WASD nesses teclados. Mudar uma tecla o torna Personalizado.",
  "Legt alle Tasten darunter auf einmal fest. WS fahren, Pfeile lenken ist die eigene Belegung des Mods; AZERTY und QWERTZ legen die Tasten dorthin, wo WASD auf diesen Tastaturen liegt. Eine geänderte Taste macht daraus Benutzerdefiniert.",
  "Définit toutes les touches ci-dessous d'un coup. WS pour rouler, flèches pour tourner est la disposition du mod ; AZERTY et QWERTZ placent les touches là où sont celles de WASD sur ces claviers. Changer une touche passe en Personnalisé.",
  "Asigna de una vez todas las teclas de abajo. WS para conducir, flechas para girar es la distribución propia del mod; AZERTY y QWERTZ ponen las teclas donde están las de WASD en esos teclados. Cambiar una tecla lo deja en Personalizado.",
  "Asigna de una vez todas las teclas de abajo. WS para manejar, flechas para girar es la distribución propia del mod; AZERTY y QWERTZ ponen las teclas donde están las de WASD en esos teclados. Cambiar una tecla lo deja en Personalizado.",
  "Imposta in una volta tutti i tasti qui sotto. WS per guidare, frecce per sterzare è la disposizione della mod; AZERTY e QWERTZ mettono i tasti dove si trovano quelli di WASD su quelle tastiere. Cambiando un tasto diventa Personalizzato.",
  "下のキーをまとめて設定します。「WSで運転、方向キーでハンドル」はこのMOD独自の配置です。AZERTY と QWERTZ は、それらのキーボードで WASD の位置にキーを割り当てます。キーを変更するとカスタムになります。",
  "아래의 모든 키를 한 번에 설정합니다. WS 주행, 방향키 조향은(는) 이 모드의 기본 배치이며, AZERTY와 QWERTZ는 해당 키보드에서 WASD 자리에 키를 둡니다. 키를 바꾸면 사용자 지정이 됩니다.",
  "一次設定下方所有按鍵。「WS 駕駛、方向鍵轉向」是此模組自己的配置；AZERTY 與 QWERTZ 會把按鍵放在這些鍵盤上 WASD 的位置。變更任一按鍵即成為自訂。",
  "Nastaví najednou všechny klávesy níže. WS jízda, šipky řízení je vlastní rozložení modu; AZERTY a QWERTZ dávají klávesy tam, kde je na těchto klávesnicích WASD. Změnou klávesy se nastaví Vlastní.",
  "Egyszerre beállítja az összes alábbi billentyűt. A WS vezetés, nyilak kormányzás a mod saját kiosztása; az AZERTY és a QWERTZ oda teszi a billentyűket, ahol azokon a billentyűzeteken a WASD van. Egy billentyű módosításával Egyéni lesz.",
  "Ustawia naraz wszystkie klawisze poniżej. WS jazda, strzałki skręt to własny układ moda; AZERTY i QWERTZ umieszczają klawisze tam, gdzie na tych klawiaturach jest WASD. Zmiana klawisza ustawia Własny.",
  "Задаёт сразу все клавиши ниже. «WS для езды, стрелки для руля» — собственная раскладка мода; AZERTY и QWERTZ ставят клавиши туда, где на этих клавиатурах находятся WASD. При изменении клавиши выбирается «Свой».",
  "Stelt alle toetsen hieronder in één keer in. WS rijden, pijlen sturen is de eigen indeling van de mod; AZERTY en QWERTZ zetten de toetsen waar WASD op die toetsenborden zit. Een toets wijzigen maakt het Aangepast."}},
// the presets (AZERTY and QWERTZ are the same everywhere)
{"WASD drive", {"Dirigir com WASD", "Fahren mit WASD", "Conduite WASD", "Conducción WASD", "Manejo WASD", "Guida WASD", "WASDで運転", "WASD 주행", "WASD 駕駛", "Jízda WASD", "WASD vezetés", "Jazda WASD", "Езда на WASD", "Rijden met WASD"}},
{"WS drive, arrow steer",
 {"WS para dirigir, setas para virar",
  "WS fahren, Pfeile lenken",
  "WS pour rouler, flèches pour tourner",
  "WS para conducir, flechas para girar",
  "WS para manejar, flechas para girar",
  "WS per guidare, frecce per sterzare",
  "WSで運転、方向キーでハンドル",
  "WS 주행, 방향키 조향",
  "WS 駕駛、方向鍵轉向",
  "WS jízda, šipky řízení",
  "WS vezetés, nyilak kormányzás",
  "WS jazda, strzałki skręt",
  "WS для езды, стрелки для руля",
  "WS rijden, pijlen sturen"}},
{"Arrow drive", {"Dirigir com as setas", "Fahren mit Pfeiltasten", "Conduite aux flèches", "Conducción con flechas", "Manejo con flechas", "Guida con le frecce", "方向キーで運転", "방향키 주행", "方向鍵駕駛", "Jízda šipkami", "Vezetés nyilakkal", "Jazda strzałkami", "Езда на стрелках", "Rijden met pijlen"}},
{"Custom", {"Personalizado", "Benutzerdefiniert", "Personnalisé", "Personalizado", "Personalizado", "Personalizzato", "カスタム", "사용자 지정", "自訂", "Vlastní", "Egyéni", "Własny", "Свой", "Aangepast"}},
// the keys' descriptions
{"FH2 ANNA's key while driving, pressing the controller's d-pad for it. The open key is ANNA's whenever you drive, the others only while ANNA's menu is open: a driving key on the same key waits till then.",
 {"A tecla do FH2 ANNA ao dirigir, que aperta o direcional do controle por ele. A tecla de abrir é da ANNA sempre que você dirige; as outras, só com o menu da ANNA aberto, e uma ação de direção na mesma tecla espera até ele fechar.",
  "Taste von FH2 ANNA beim Fahren; sie drückt dafür das Steuerkreuz des Controllers. Die Öffnen-Taste gehört beim Fahren immer ANNA, die anderen nur bei geöffnetem ANNA-Menü: Eine Fahrfunktion auf derselben Taste ruht so lange.",
  "Touche de FH2 ANNA en conduite ; elle appuie à sa place sur la croix directionnelle de la manette. La touche d'ouverture est à ANNA dès que vous conduisez, les autres seulement quand le menu d'ANNA est ouvert : une commande de conduite sur la même touche est alors suspendue.",
  "Tecla de FH2 ANNA al conducir; pulsa por él la cruceta del mando. La tecla de abrir es de ANNA siempre que conduces; las demás, solo con el menú de ANNA abierto: una acción de conducción en la misma tecla queda en pausa mientras tanto.",
  "Tecla de FH2 ANNA al manejar; presiona por él la cruceta del control. La tecla de abrir es de ANNA siempre que manejas; las demás, solo con el menú de ANNA abierto: una acción de manejo en la misma tecla queda en pausa mientras tanto.",
  "Tasto di FH2 ANNA durante la guida; preme al suo posto la croce direzionale del controller. Il tasto di apertura è di ANNA ogni volta che guidi, gli altri solo con il menu di ANNA aperto: un comando di guida sullo stesso tasto resta sospeso nel frattempo.",
  "運転中の FH2 ANNA のキーで、代わりにコントローラーの十字キーを押します。開くキーは運転中いつでも ANNA のもの、その他は ANNA のメニューが開いている間だけです。同じキーの運転操作はその間は働きません。",
  "운전 중 FH2 ANNA의 키로, 컨트롤러의 방향 패드를 대신 누릅니다. 열기 키는 운전 중 항상 ANNA가 사용하고, 나머지는 ANNA 메뉴가 열려 있을 때만 사용합니다. 같은 키의 주행 조작은 그동안 작동하지 않습니다.",
  "駕駛時 FH2 ANNA 的按鍵，會代為按下控制器的方向鍵。開啟鍵在駕駛時一直屬於 ANNA，其他按鍵只在 ANNA 選單開啟時屬於它：同一按鍵上的駕駛操作在此期間暫停。",
  "Klávesa FH2 ANNA při jízdě; mačká za ni směrový ovladač. Klávesa otevření patří ANNA vždy při jízdě, ostatní jen při otevřené nabídce ANNA: jízdní akce na stejné klávese mezitím čeká.",
  "Az FH2 ANNA billentyűje vezetés közben; helyette a kontroller iránygombjait nyomja. A megnyitó billentyű vezetés közben mindig az ANNA-é, a többi csak nyitott ANNA menü mellett: az ugyanarra a billentyűre tett vezetési művelet addig szünetel.",
  "Klawisz FH2 ANNA podczas jazdy; naciska za nią krzyżak kontrolera. Klawisz otwierania należy do ANNA zawsze podczas jazdy, pozostałe tylko przy otwartym menu ANNA: akcja jazdy na tym samym klawiszu jest w tym czasie wstrzymana.",
  "Клавиша FH2 ANNA во время езды; нажимает за неё крестовину контроллера. Клавиша открытия принадлежит ANNA всегда, пока вы едете, остальные только при открытом меню ANNA: действие езды на той же клавише в это время не работает.",
  "Toets van FH2 ANNA tijdens het rijden; drukt daarvoor op de d-pad van de controller. De open-toets is van ANNA zolang je rijdt, de andere alleen als ANNA's menu open is: een rijfunctie op dezelfde toets wacht zolang."}},
{"ANNA Drone Mode's key while flying the drone. While it flies only its keys (and the radio's) count, so it can be a driving or menu key too.",
 {"A tecla do ANNA Drone Mode ao pilotar o drone. Enquanto ele voa, só as teclas dele (e as do rádio) contam, então ela pode ser também uma tecla de direção ou de menu.",
  "Taste von ANNA Drone Mode beim Fliegen der Drohne. Während sie fliegt, zählen nur ihre Tasten (und die des Radios), daher kann es auch eine Fahr- oder Menütaste sein.",
  "Touche d'ANNA Drone Mode pendant le vol du drone. Pendant qu'il vole, seules ses touches (et celles de la radio) comptent : elle peut donc aussi être une touche de conduite ou de menu.",
  "Tecla de ANNA Drone Mode al volar el dron. Mientras vuela solo cuentan sus teclas (y las de la radio), así que también puede ser una tecla de conducción o de menú.",
  "Tecla de ANNA Drone Mode al volar el dron. Mientras vuela solo cuentan sus teclas (y las de la radio), así que también puede ser una tecla de manejo o de menú.",
  "Tasto di ANNA Drone Mode durante il volo del drone. Mentre vola contano solo i suoi tasti (e quelli della radio), quindi può essere anche un tasto di guida o del menu.",
  "ANNA Drone Mode でドローンを飛ばしている間のキー。飛行中はそのキー (とラジオのキー) だけが働くので、運転やメニューのキーと同じでもかまいません。",
  "ANNA Drone Mode로 드론을 조종하는 동안의 키입니다. 비행 중에는 드론 키(와 라디오 키)만 작동하므로 주행이나 메뉴 키와 같아도 됩니다.",
  "ANNA Drone Mode 操控無人機時的按鍵。飛行時只有它的按鍵（與電台按鍵）有作用，因此也可以與駕駛或選單按鍵相同。",
  "Klávesa ANNA Drone Mode při létání s dronem. Za letu platí jen jeho klávesy (a klávesy rádia), takže může být i jízdní nebo nabídkovou klávesou.",
  "Az ANNA Drone Mode billentyűje a drón repítése közben. Repülés közben csak az ő billentyűi (és a rádióé) számítanak, így vezetési vagy menübillentyű is lehet.",
  "Klawisz ANNA Drone Mode podczas lotu dronem. W trakcie lotu liczą się tylko jego klawisze (i klawisze radia), więc może to być też klawisz jazdy lub menu.",
  "Клавиша ANNA Drone Mode во время полёта дрона. Пока он летает, работают только его клавиши (и клавиши радио), поэтому она может совпадать с клавишей езды или меню.",
  "Toets van ANNA Drone Mode tijdens het vliegen met de drone. Zolang die vliegt tellen alleen zijn toetsen (en die van de radio), dus het mag ook een rij- of menutoets zijn."}},
{"The key for this in the menus. It does nothing while driving, so it can be a driving key too.",
 {"A tecla para isto nos menus. Não faz nada ao dirigir, então pode ser também uma tecla de direção.",
  "Die Taste dafür in den Menüs. Beim Fahren tut sie nichts, kann also auch eine Fahrtaste sein.",
  "La touche pour ceci dans les menus. Elle ne fait rien en conduite, elle peut donc aussi servir à conduire.",
  "La tecla para esto en los menús. No hace nada al conducir, así que también puede ser una tecla de conducción.",
  "La tecla para esto en los menús. No hace nada al manejar, así que también puede ser una tecla de manejo.",
  "Il tasto per questo nei menu. Non fa nulla durante la guida, quindi può essere anche un tasto di guida.",
  "メニューでのこの操作のキーです。運転中は何もしないので、運転用のキーと同じでもかまいません。",
  "메뉴에서 이 동작에 쓰는 키입니다. 운전 중에는 아무 일도 하지 않으므로 주행 키와 같아도 됩니다.",
  "選單中此操作的按鍵。駕駛時沒有作用，因此也可以是駕駛按鍵。",
  "Klávesa pro tuto akci v nabídkách. Při jízdě nic nedělá, takže může být i jízdní klávesou.",
  "Ennek a billentyűje a menükben. Vezetés közben nem csinál semmit, így vezetési billentyű is lehet.",
  "Klawisz tej akcji w menu. Podczas jazdy nic nie robi, więc może też być klawiszem jazdy.",
  "Клавиша для этого в меню. Во время езды она ничего не делает, поэтому может быть и клавишей езды.",
  "De toets hiervoor in de menu's. Tijdens het rijden doet hij niets, dus het mag ook een rijtoets zijn."}},
{"The key for this while driving: Extra Options' own, pressed on the controller button its ini gives it. It does nothing in the menus, so it can be a menu key too.",
 {"A tecla para isto ao dirigir: a do próprio Extra Options, apertada no botão do controle que o ini dele define. Não faz nada nos menus, então pode ser também uma tecla de menu.",
  "Die Taste dafür beim Fahren: die von Extra Options, gedrückt auf der Controllertaste, die dessen INI vorgibt. In den Menüs tut sie nichts, kann also auch eine Menütaste sein.",
  "La touche pour ceci en conduite : celle d'Extra Options, appuyée sur le bouton de manette que lui donne son ini. Elle ne fait rien dans les menus, elle peut donc aussi être une touche de menu.",
  "La tecla para esto al conducir: la propia de Extra Options, pulsada en el botón del mando que le asigna su ini. No hace nada en los menús, así que también puede ser una tecla de menú.",
  "La tecla para esto al manejar: la propia de Extra Options, presionada en el botón del control que le asigna su ini. No hace nada en los menús, así que también puede ser una tecla de menú.",
  "Il tasto per questo durante la guida: quello di Extra Options, premuto sul pulsante del controller indicato dal suo ini. Non fa nulla nei menu, quindi può essere anche un tasto dei menu.",
  "運転中のこの操作のキーです。Extra Options 自身の操作で、その ini が割り当てたコントローラーのボタンを押します。メニューでは何もしないので、メニュー用のキーと同じでもかまいません。",
  "운전 중 이 동작에 쓰는 키입니다. Extra Options 자체 기능으로, 그 ini에 지정된 컨트롤러 버튼을 누릅니다. 메뉴에서는 아무 일도 하지 않으므로 메뉴 키와 같아도 됩니다.",
  "駕駛時此操作的按鍵：Extra Options 自己的功能，會按下其 ini 指定的控制器按鈕。在選單中沒有作用，因此也可以是選單按鍵。",
  "Klávesa pro tuto akci při jízdě: vlastní funkce Extra Options, stiskne tlačítko ovladače, které jí dává její ini. V nabídkách nic nedělá, takže může být i klávesou nabídek.",
  "Ennek a billentyűje vezetés közben: az Extra Options saját funkciója, a kontrollernek azt a gombját nyomja, amelyet az ini-je megad. A menükben nem csinál semmit, így menübillentyű is lehet.",
  "Klawisz tej akcji podczas jazdy: własna funkcja Extra Options, naciskająca przycisk kontrolera wskazany w jego ini. W menu nic nie robi, więc może też być klawiszem menu.",
  "Клавиша для этого во время езды: собственная функция Extra Options, нажимающая кнопку контроллера, заданную в её ini. В меню она ничего не делает, поэтому может быть и клавишей меню.",
  "De toets hiervoor tijdens het rijden: die van Extra Options zelf, ingedrukt op de controllerknop die zijn ini aangeeft. In de menu's doet hij niets, dus het mag ook een menutoets zijn."}},
{"The key for this while driving. It does nothing in the menus, so it can be a menu key too.",
 {"A tecla para isto ao dirigir. Não faz nada nos menus, então pode ser também uma tecla de menu.",
  "Die Taste dafür beim Fahren. In den Menüs tut sie nichts, kann also auch eine Menütaste sein.",
  "La touche pour ceci en conduite. Elle ne fait rien dans les menus, elle peut donc aussi être une touche de menu.",
  "La tecla para esto al conducir. No hace nada en los menús, así que también puede ser una tecla de menú.",
  "La tecla para esto al manejar. No hace nada en los menús, así que también puede ser una tecla de menú.",
  "Il tasto per questo durante la guida. Non fa nulla nei menu, quindi può essere anche un tasto dei menu.",
  "運転中のこの操作のキーです。メニューでは何もしないので、メニュー用のキーと同じでもかまいません。",
  "운전 중 이 동작에 쓰는 키입니다. 메뉴에서는 아무 일도 하지 않으므로 메뉴 키와 같아도 됩니다.",
  "駕駛時此操作的按鍵。在選單中沒有作用，因此也可以是選單按鍵。",
  "Klávesa pro tuto akci při jízdě. V nabídkách nic nedělá, takže může být i klávesou nabídek.",
  "Ennek a billentyűje vezetés közben. A menükben nem csinál semmit, így menübillentyű is lehet.",
  "Klawisz tej akcji podczas jazdy. W menu nic nie robi, więc może też być klawiszem menu.",
  "Клавиша для этого во время езды. В меню она ничего не делает, поэтому может быть и клавишей меню.",
  "De toets hiervoor tijdens het rijden. In de menu's doet hij niets, dus het mag ook een menutoets zijn."}},
// the keys (kActions' labels)
{"Gas", {"Acelerar", "Gas", "Accélérer", "Acelerar", "Acelerar", "Accelera", "アクセル", "가속", "油門", "Plyn", "Gáz", "Gaz", "Газ", "Gas"}},
{"Brake / reverse", {"Frear / ré", "Bremse / Rückwärts", "Frein / marche arrière", "Freno / marcha atrás", "Freno / reversa", "Freno / retromarcia", "ブレーキ / バック", "브레이크 / 후진", "煞車 / 倒車", "Brzda / couvání", "Fék / tolatás", "Hamulec / wsteczny", "Тормоз / задний ход", "Remmen / achteruit"}},
{"Steer left", {"Virar à esquerda", "Links lenken", "Tourner à gauche", "Girar a la izquierda", "Girar a la izquierda", "Sterza a sinistra", "左にハンドル", "왼쪽으로 조향", "向左轉", "Zatočit vlevo", "Kormányzás balra", "Skręt w lewo", "Руль влево", "Naar links sturen"}},
{"Steer right", {"Virar à direita", "Rechts lenken", "Tourner à droite", "Girar a la derecha", "Girar a la derecha", "Sterza a destra", "右にハンドル", "오른쪽으로 조향", "向右轉", "Zatočit vpravo", "Kormányzás jobbra", "Skręt w prawo", "Руль вправо", "Naar rechts sturen"}},
{"Handbrake", {"Freio de mão", "Handbremse", "Frein à main", "Freno de mano", "Freno de mano", "Freno a mano", "サイドブレーキ", "핸드브레이크", "手煞車", "Ruční brzda", "Kézifék", "Hamulec ręczny", "Ручной тормоз", "Handrem"}},
{"Shift up", {"Subir marcha", "Hochschalten", "Monter un rapport", "Subir marcha", "Subir velocidad", "Marcia su", "シフトアップ", "기어 올리기", "升檔", "Řadit nahoru", "Felváltás", "Bieg w górę", "Передача вверх", "Opschakelen"}},
{"Shift down", {"Reduzir marcha", "Herunterschalten", "Rétrograder", "Bajar marcha", "Bajar velocidad", "Marcia giù", "シフトダウン", "기어 내리기", "降檔", "Řadit dolů", "Visszaváltás", "Bieg w dół", "Передача вниз", "Terugschakelen"}},
{"Clutch", {"Embreagem", "Kupplung", "Embrayage", "Embrague", "Clutch", "Frizione", "クラッチ", "클러치", "離合器", "Spojka", "Kuplung", "Sprzęgło", "Сцепление", "Koppeling"}},
{"Change camera", {"Mudar câmera", "Kamera wechseln", "Changer de caméra", "Cambiar cámara", "Cambiar cámara", "Cambia telecamera", "カメラ切り替え", "카메라 변경", "切換鏡頭", "Změnit kameru", "Kameraváltás", "Zmień kamerę", "Сменить камеру", "Camera wisselen"}},
{"Look back", {"Olhar para trás", "Nach hinten schauen", "Regarder derrière", "Mirar atrás", "Mirar atrás", "Guarda indietro", "後方を見る", "뒤 보기", "向後看", "Pohled dozadu", "Hátranézés", "Spójrz do tyłu", "Смотреть назад", "Achteruit kijken"}},
{"Look left", {"Olhar para a esquerda", "Nach links schauen", "Regarder à gauche", "Mirar a la izquierda", "Mirar a la izquierda", "Guarda a sinistra", "左を見る", "왼쪽 보기", "向左看", "Pohled doleva", "Balra nézés", "Spójrz w lewo", "Смотреть влево", "Naar links kijken"}},
{"Look right", {"Olhar para a direita", "Nach rechts schauen", "Regarder à droite", "Mirar a la derecha", "Mirar a la derecha", "Guarda a destra", "右を見る", "오른쪽 보기", "向右看", "Pohled doprava", "Jobbra nézés", "Spójrz w prawo", "Смотреть вправо", "Naar rechts kijken"}},
{"Rewind", {"Rebobinar", "Zurückspulen", "Rembobiner", "Rebobinar", "Rebobinar", "Riavvolgi", "巻き戻し", "되감기", "倒帶", "Přetočit", "Visszatekerés", "Przewiń", "Перемотка", "Terugspoelen"}},
{"Map", {"Mapa", "Karte", "Carte", "Mapa", "Mapa", "Mappa", "マップ", "지도", "地圖", "Mapa", "Térkép", "Mapa", "Карта", "Kaart"}},
{"Photo mode", {"Modo foto", "Fotomodus", "Mode photo", "Modo foto", "Modo foto", "Modalità foto", "フォトモード", "사진 모드", "相片模式", "Fotorežim", "Fotómód", "Tryb fotograficzny", "Фоторежим", "Fotomodus"}},
{"Previous station", {"Estação anterior", "Vorheriger Sender", "Station précédente", "Emisora anterior", "Estación anterior", "Stazione precedente", "前の局", "이전 방송국", "上一個電台", "Předchozí stanice", "Előző állomás", "Poprzednia stacja", "Предыдущая станция", "Vorige zender"}},
{"Next station", {"Próxima estação", "Nächster Sender", "Station suivante", "Emisora siguiente", "Estación siguiente", "Stazione successiva", "次の局", "다음 방송국", "下一個電台", "Další stanice", "Következő állomás", "Następna stacja", "Следующая станция", "Volgende zender"}},
{"Telemetry", {"Telemetria", "Telemetrie", "Télémétrie", "Telemetría", "Telemetría", "Telemetria", "テレメトリー", "텔레메트리", "車輛資料", "Telemetrie", "Telemetria", "Telemetria", "Телеметрия", "Telemetrie"}},
{"Enter garage (prompts)",
 {"Entrar na garagem (avisos)",
  "Garage betreten (Hinweise)",
  "Entrer au garage (invites)",
  "Entrar al garaje (avisos)",
  "Entrar al garage (avisos)",
  "Entra in garage (avvisi)",
  "ガレージに入る (案内)",
  "차고 들어가기(안내)",
  "進入車庫 (提示)",
  "Vjet do garáže (výzvy)",
  "Belépés a garázsba (felszólítások)",
  "Wjedź do garażu (podpowiedzi)",
  "Въезд в гараж (подсказки)",
  "Garage in (meldingen)"}},
{"Nitro (FnFNitro)", {"Nitro (FnFNitro)", "Nitro (FnFNitro)", "Nitro (FnFNitro)", "Nitro (FnFNitro)", "Nitro (FnFNitro)", "Nitro (FnFNitro)", "ニトロ (FnFNitro)", "니트로 (FnFNitro)", "加速 (FnFNitro)", "Nitro (FnFNitro)", "Nitro (FnFNitro)", "Nitro (FnFNitro)", "Нитро (FnFNitro)", "Nitro (FnFNitro)"}},
{"Horn (HonkHonk)", {"Buzina (HonkHonk)", "Hupe (HonkHonk)", "Klaxon (HonkHonk)", "Claxon (HonkHonk)", "Claxon (HonkHonk)", "Clacson (HonkHonk)", "クラクション (HonkHonk)", "경적 (HonkHonk)", "警示音 (HonkHonk)", "Klakson (HonkHonk)", "Duda (HonkHonk)", "Klakson (HonkHonk)", "Гудок (HonkHonk)", "Claxon (HonkHonk)"}},
{"Left indicator", {"Seta esquerda", "Blinker links", "Clignotant gauche", "Intermitente izquierdo", "Direccional izquierda", "Freccia sinistra", "左ウインカー", "왼쪽 방향 지시등", "左方向燈", "Levý blinkr", "Bal index", "Lewy kierunkowskaz", "Левый поворотник", "Richtingaanwijzer links"}},
{"Right indicator", {"Seta direita", "Blinker rechts", "Clignotant droit", "Intermitente derecho", "Direccional derecha", "Freccia destra", "右ウインカー", "오른쪽 방향 지시등", "右方向燈", "Pravý blinkr", "Jobb index", "Prawy kierunkowskaz", "Правый поворотник", "Richtingaanwijzer rechts"}},
{"Hazard lights", {"Pisca-alerta", "Warnblinker", "Feux de détresse", "Luces de emergencia", "Intermitentes", "Luci di emergenza", "ハザードランプ", "비상등", "警示燈", "Výstražná světla", "Vészvillogó", "Światła awaryjne", "Аварийная сигнализация", "Alarmlichten"}},
{"Headlights", {"Faróis", "Scheinwerfer", "Phares", "Faros", "Faros", "Fari", "ヘッドライト", "헤드라이트", "頭燈", "Světlomety", "Fényszórók", "Reflektory", "Фары", "Koplampen"}},
{"ANNA: open / close", {"ANNA: abrir / fechar", "ANNA: öffnen / schließen", "ANNA : ouvrir / fermer", "ANNA: abrir / cerrar", "ANNA: abrir / cerrar", "ANNA: apri / chiudi", "ANNA: 開く / 閉じる", "ANNA: 열기 / 닫기", "ANNA：開啟 / 關閉", "ANNA: otevřít / zavřít", "ANNA: megnyitás / bezárás", "ANNA: otwórz / zamknij", "ANNA: открыть / закрыть", "ANNA: openen / sluiten"}},
{"ANNA: up", {"ANNA: cima", "ANNA: hoch", "ANNA : haut", "ANNA: arriba", "ANNA: arriba", "ANNA: su", "ANNA: 上", "ANNA: 위", "ANNA：上", "ANNA: nahoru", "ANNA: fel", "ANNA: w górę", "ANNA: вверх", "ANNA: omhoog"}},
{"ANNA: down", {"ANNA: baixo", "ANNA: runter", "ANNA : bas", "ANNA: abajo", "ANNA: abajo", "ANNA: giù", "ANNA: 下", "ANNA: 아래", "ANNA：下", "ANNA: dolů", "ANNA: le", "ANNA: w dół", "ANNA: вниз", "ANNA: omlaag"}},
{"ANNA: back", {"ANNA: voltar", "ANNA: zurück", "ANNA : retour", "ANNA: atrás", "ANNA: atrás", "ANNA: indietro", "ANNA: 戻る", "ANNA: 뒤로", "ANNA：返回", "ANNA: zpět", "ANNA: vissza", "ANNA: wstecz", "ANNA: назад", "ANNA: terug"}},
{"ANNA: choose", {"ANNA: escolher", "ANNA: auswählen", "ANNA : choisir", "ANNA: elegir", "ANNA: elegir", "ANNA: scegli", "ANNA: 決定", "ANNA: 선택", "ANNA：選擇", "ANNA: vybrat", "ANNA: kiválasztás", "ANNA: wybierz", "ANNA: выбрать", "ANNA: kiezen"}},
{"ANNA: cinematic camera (autodrive)", {"ANNA: câmera cinematográfica (direção automática)", "ANNA: Kinokamera (Autopilot)", "ANNA : caméra cinématique (conduite auto)", "ANNA: cámara cinemática (conducción automática)", "ANNA: cámara cinemática (manejo automático)", "ANNA: telecamera cinematografica (guida automatica)", "ANNA: シネマカメラ（自動運転）", "ANNA: 시네마틱 카메라 (자동 주행)", "ANNA：電影鏡頭（自動駕駛）", "ANNA: filmová kamera (autopilot)", "ANNA: filmes kamera (robotpilóta)", "ANNA: kamera filmowa (autopilot)", "ANNA: кинокамера (автопилот)", "ANNA: filmische camera (automatisch rijden)"}},
{"FH2 ANNA's key while its autodrive drives: turns the cinematic camera on and off, holding the controller button ANNA's ini gives it (LB). It does nothing otherwise, so it can be a driving key too.",
 {"Tecla do FH2 ANNA durante a direção automática: liga e desliga a câmera cinematográfica, segurando o botão do controle definido no ini da ANNA (LB). Fora disso não faz nada, então pode ser também uma tecla de direção.",
  "FH2 ANNAs Taste während des Autopiloten: schaltet die Kinokamera ein und aus und hält dazu die Controllertaste aus ANNAs ini (LB). Sonst tut sie nichts, kann also auch eine Fahrtaste sein.",
  "Touche de FH2 ANNA pendant sa conduite auto : active et désactive la caméra cinématique en maintenant le bouton de manette que donne l'ini d'ANNA (LB). Sinon elle ne fait rien, elle peut donc aussi servir à la conduite.",
  "Tecla de FH2 ANNA durante su conducción automática: activa y desactiva la cámara cinemática pulsando el botón del mando que indica el ini de ANNA (LB). Si no, no hace nada, así que puede ser también una tecla de conducción.",
  "Tecla de FH2 ANNA durante su manejo automático: activa y desactiva la cámara cinemática presionando el botón del control que indica el ini de ANNA (LB). Si no, no hace nada, así que puede ser también una tecla de manejo.",
  "Tasto di FH2 ANNA durante la sua guida automatica: attiva e disattiva la telecamera cinematografica tenendo premuto il pulsante del controller indicato nell'ini di ANNA (LB). Altrimenti non fa nulla, quindi può essere anche un tasto di guida.",
  "FH2 ANNAの自動運転中のキー: ANNAのiniで指定したコントローラーのボタン（LB）を押してシネマカメラをオン／オフします。それ以外では何もしないので、運転用のキーと兼用できます。",
  "FH2 ANNA 자동 주행 중의 키: ANNA ini에 지정된 컨트롤러 버튼(LB)을 눌러 시네마틱 카메라를 켜고 끕니다. 그 외에는 아무 동작도 하지 않으므로 운전 키와 같이 써도 됩니다.",
  "FH2 ANNA 自動駕駛時的按鍵：按住 ANNA ini 指定的控制器按鈕（LB）來開關電影鏡頭。其他時候不做任何事，所以也可以當駕駛按鍵。",
  "Klávesa FH2 ANNA během autopilota: zapíná a vypíná filmovou kameru podržením tlačítka ovladače z ini ANNA (LB). Jinak nedělá nic, takže může být i klávesou pro řízení.",
  "Az FH2 ANNA billentyűje robotpilóta közben: az ANNA ini-jében megadott kontrollergomb (LB) nyomásával be- és kikapcsolja a filmes kamerát. Máskor nem csinál semmit, így vezetési billentyű is lehet.",
  "Klawisz FH2 ANNA podczas autopilota: włącza i wyłącza kamerę filmową, przytrzymując przycisk kontrolera z ini ANNA (LB). Poza tym nic nie robi, więc może być też klawiszem jazdy.",
  "Клавиша FH2 ANNA во время автопилота: включает и выключает кинокамеру, удерживая кнопку контроллера из ini ANNA (LB). В остальное время ничего не делает, поэтому может быть и клавишей вождения.",
  "Toets van FH2 ANNA tijdens automatisch rijden: zet de filmische camera aan en uit door de controllerknop uit ANNA's ini (LB) in te drukken. Verder doet hij niets, dus hij kan ook een rijtoets zijn."}},
{"Drone: forward", {"Drone: frente", "Drohne: vorwärts", "Drone : avant", "Dron: adelante", "Dron: adelante", "Drone: avanti", "ドローン: 前進", "드론: 앞으로", "無人機：前進", "Dron: vpřed", "Drón: előre", "Dron: do przodu", "Дрон: вперёд", "Drone: vooruit"}},
{"Drone: back", {"Drone: trás", "Drohne: rückwärts", "Drone : arrière", "Dron: atrás", "Dron: atrás", "Drone: indietro", "ドローン: 後退", "드론: 뒤로", "無人機：後退", "Dron: vzad", "Drón: hátra", "Dron: do tyłu", "Дрон: назад", "Drone: achteruit"}},
{"Drone: left", {"Drone: esquerda", "Drohne: links", "Drone : gauche", "Dron: izquierda", "Dron: izquierda", "Drone: sinistra", "ドローン: 左", "드론: 왼쪽", "無人機：左", "Dron: vlevo", "Drón: balra", "Dron: w lewo", "Дрон: влево", "Drone: links"}},
{"Drone: right", {"Drone: direita", "Drohne: rechts", "Drone : droite", "Dron: derecha", "Dron: derecha", "Drone: destra", "ドローン: 右", "드론: 오른쪽", "無人機：右", "Dron: vpravo", "Drón: jobbra", "Dron: w prawo", "Дрон: вправо", "Drone: rechts"}},
{"Drone: up", {"Drone: subir", "Drohne: hoch", "Drone : monter", "Dron: subir", "Dron: subir", "Drone: su", "ドローン: 上昇", "드론: 위로", "無人機：上升", "Dron: nahoru", "Drón: fel", "Dron: w górę", "Дрон: вверх", "Drone: omhoog"}},
{"Drone: down", {"Drone: descer", "Drohne: runter", "Drone : descendre", "Dron: bajar", "Dron: bajar", "Drone: giù", "ドローン: 下降", "드론: 아래로", "無人機：下降", "Dron: dolů", "Drón: le", "Dron: w dół", "Дрон: вниз", "Drone: omlaag"}},
{"Drone: faster", {"Drone: mais rápido", "Drohne: schneller", "Drone : plus vite", "Dron: más rápido", "Dron: más rápido", "Drone: più veloce", "ドローン: 高速", "드론: 더 빠르게", "無人機：加速", "Dron: rychleji", "Drón: gyorsabban", "Dron: szybciej", "Дрон: быстрее", "Drone: sneller"}},
{"Drone: back to the car", {"Drone: voltar ao carro", "Drohne: zurück zum Auto", "Drone : retour à la voiture", "Dron: volver al coche", "Dron: volver al auto", "Drone: torna all'auto", "ドローン: 車に戻る", "드론: 차로 돌아가기", "無人機：回到車輛", "Dron: zpět k autu", "Drón: vissza az autóhoz", "Dron: powrót do auta", "Дрон: к машине", "Drone: terug naar de auto"}},
{"Drone: look at the car", {"Drone: olhar para o carro", "Drohne: zum Auto schauen", "Drone : regarder la voiture", "Dron: mirar al coche", "Dron: mirar al auto", "Drone: guarda l'auto", "ドローン: 車を見る", "드론: 차 바라보기", "無人機：看向車輛", "Dron: dívat se na auto", "Drón: az autó felé néz", "Dron: patrz na auto", "Дрон: смотреть на машину", "Drone: naar de auto kijken"}},
{"Drone: hide / show the help", {"Drone: ocultar / mostrar a ajuda", "Drohne: Hilfe aus / ein", "Drone : masquer / afficher l'aide", "Dron: ocultar / mostrar la ayuda", "Dron: ocultar / mostrar la ayuda", "Drone: nascondi / mostra l'aiuto", "ドローン: ヘルプの非表示 / 表示", "드론: 도움말 숨기기 / 표시", "無人機：隱藏 / 顯示說明", "Dron: skrýt / zobrazit nápovědu", "Drón: súgó elrejtése / megjelenítése", "Dron: ukryj / pokaż pomoc", "Дрон: скрыть / показать подсказки", "Drone: hulp verbergen / tonen"}},
{"Drone: exit", {"Drone: sair", "Drohne: beenden", "Drone : quitter", "Dron: salir", "Dron: salir", "Drone: esci", "ドローン: 終了", "드론: 나가기", "無人機：離開", "Dron: ukončit", "Drón: kilépés", "Dron: wyjdź", "Дрон: выйти", "Drone: afsluiten"}},
{"Pause", {"Pausar", "Pause", "Pause", "Pausa", "Pausa", "Pausa", "ポーズ", "일시 정지", "暫停", "Pauza", "Szünet", "Pauza", "Пауза", "Pauze"}},
{"Menu: select", {"Menu: selecionar", "Menü: auswählen", "Menu : sélectionner", "Menú: seleccionar", "Menú: seleccionar", "Menu: seleziona", "メニュー: 決定", "메뉴: 선택", "選單：選擇", "Nabídka: vybrat", "Menü: kiválasztás", "Menu: wybierz", "Меню: выбрать", "Menu: selecteren"}},
{"Menu: back", {"Menu: voltar", "Menü: zurück", "Menu : retour", "Menú: atrás", "Menú: atrás", "Menu: indietro", "メニュー: 戻る", "메뉴: 뒤로", "選單：返回", "Nabídka: zpět", "Menü: vissza", "Menu: wstecz", "Меню: назад", "Menu: terug"}},
{"Menu: up", {"Menu: cima", "Menü: hoch", "Menu : haut", "Menú: arriba", "Menú: arriba", "Menu: su", "メニュー: 上", "메뉴: 위", "選單：上", "Nabídka: nahoru", "Menü: fel", "Menu: w górę", "Меню: вверх", "Menu: omhoog"}},
{"Menu: down", {"Menu: baixo", "Menü: runter", "Menu : bas", "Menú: abajo", "Menú: abajo", "Menu: giù", "メニュー: 下", "메뉴: 아래", "選單：下", "Nabídka: dolů", "Menü: le", "Menu: w dół", "Меню: вниз", "Menu: omlaag"}},
{"Menu: left", {"Menu: esquerda", "Menü: links", "Menu : gauche", "Menú: izquierda", "Menú: izquierda", "Menu: sinistra", "メニュー: 左", "메뉴: 왼쪽", "選單：左", "Nabídka: doleva", "Menü: balra", "Menu: w lewo", "Меню: влево", "Menu: links"}},
{"Menu: right", {"Menu: direita", "Menü: rechts", "Menu : droite", "Menú: derecha", "Menú: derecha", "Menu: destra", "メニュー: 右", "메뉴: 오른쪽", "選單：右", "Nabídka: doprava", "Menü: jobbra", "Menu: w prawo", "Меню: вправо", "Menu: rechts"}},
{"Menu: previous tab", {"Menu: aba anterior", "Menü: vorheriger Tab", "Menu : onglet précédent", "Menú: pestaña anterior", "Menú: pestaña anterior", "Menu: scheda precedente", "メニュー: 前のタブ", "메뉴: 이전 탭", "選單：上一個分頁", "Nabídka: předchozí karta", "Menü: előző fül", "Menu: poprzednia karta", "Меню: предыдущая вкладка", "Menu: vorig tabblad"}},
{"Menu: next tab", {"Menu: próxima aba", "Menü: nächster Tab", "Menu : onglet suivant", "Menú: pestaña siguiente", "Menú: pestaña siguiente", "Menu: scheda successiva", "メニュー: 次のタブ", "메뉴: 다음 탭", "選單：下一個分頁", "Nabídka: další karta", "Menü: következő fül", "Menu: następna karta", "Меню: следующая вкладка", "Menu: volgend tabblad"}},
{"Menu: X button", {"Menu: botão X", "Menü: X-Taste", "Menu : bouton X", "Menú: botón X", "Menú: botón X", "Menu: pulsante X", "メニュー: Xボタン", "메뉴: X 버튼", "選單：X 按鈕", "Nabídka: tlačítko X", "Menü: X gomb", "Menu: przycisk X", "Меню: кнопка X", "Menu: X-knop"}},
{"Menu: Y button", {"Menu: botão Y", "Menü: Y-Taste", "Menu : bouton Y", "Menú: botón Y", "Menú: botón Y", "Menu: pulsante Y", "メニュー: Yボタン", "메뉴: Y 버튼", "選單：Y 按鈕", "Nabídka: tlačítko Y", "Menü: Y gomb", "Menu: przycisk Y", "Меню: кнопка Y", "Menu: Y-knop"}},
{"Menu: View button", {"Menu: botão Exibir", "Menü: Ansicht-Taste", "Menu : bouton Affichage", "Menú: botón Vista", "Menú: botón Vista", "Menu: pulsante Visualizza", "メニュー: ビューボタン", "메뉴: 보기 버튼", "選單：檢視按鈕", "Nabídka: tlačítko Zobrazit", "Menü: Nézet gomb", "Menu: przycisk Widok", "Меню: кнопка Просмотр", "Menu: Weergave-knop"}},
{"Menu: left trigger", {"Menu: gatilho esquerdo", "Menü: linker Trigger", "Menu : gâchette gauche", "Menú: gatillo izquierdo", "Menú: gatillo izquierdo", "Menu: grilletto sinistro", "メニュー: 左トリガー", "메뉴: 왼쪽 트리거", "選單：左觸發鍵", "Nabídka: levá spoušť", "Menü: bal ravasz", "Menu: lewy spust", "Меню: левый курок", "Menu: linkertrekker"}},
{"Menu: right trigger", {"Menu: gatilho direito", "Menü: rechter Trigger", "Menu : gâchette droite", "Menú: gatillo derecho", "Menú: gatillo derecho", "Menu: grilletto destro", "メニュー: 右トリガー", "메뉴: 오른쪽 트리거", "選單：右觸發鍵", "Nabídka: pravá spoušť", "Menü: jobb ravasz", "Menu: prawy spust", "Меню: правый курок", "Menu: rechtertrekker"}},
{"Menu: left stick click",
 {"Menu: clique analógico esq.",
  "Menü: linken Stick drücken",
  "Menu : clic du stick gauche",
  "Menú: pulsar stick izquierdo",
  "Menú: presionar stick izquierdo",
  "Menu: clic levetta sinistra",
  "メニュー: 左スティック押し込み",
  "메뉴: 왼쪽 스틱 누르기",
  "選單：按下左搖桿",
  "Nabídka: stisk levé páčky",
  "Menü: bal kar lenyomása",
  "Menu: wciśnięcie lewej gałki",
  "Меню: нажатие левого стика",
  "Menu: linkerstick indrukken"}},
{"Menu: right stick click",
 {"Menu: clique analógico dir.",
  "Menü: rechten Stick drücken",
  "Menu : clic du stick droit",
  "Menú: pulsar stick derecho",
  "Menú: presionar stick derecho",
  "Menu: clic levetta destra",
  "メニュー: 右スティック押し込み",
  "메뉴: 오른쪽 스틱 누르기",
  "選單：按下右搖桿",
  "Nabídka: stisk pravé páčky",
  "Menü: jobb kar lenyomása",
  "Menu: wciśnięcie prawej gałki",
  "Меню: нажатие правого стика",
  "Menu: rechterstick indrukken"}},
// the controller's layout
{"Controller layout", {"Layout do controle", "Controller-Belegung", "Disposition de la manette", "Configuración del mando", "Configuración del control", "Layout del controller", "コントローラー配置", "컨트롤러 배치", "控制器配置", "Rozložení ovladače", "Kontrollerkiosztás", "Układ kontrolera", "Раскладка контроллера", "Controllerindeling"}},
{"Game's", {"Do jogo", "Vom Spiel", "Du jeu", "Del juego", "Del juego", "Del gioco", "ゲーム設定", "게임 설정", "遊戲設定", "Ze hry", "A játéké", "Z gry", "Из игры", "Van de game"}},
{"None", {"Nenhuma", "Keine", "Aucune", "Ninguna", "Ninguna", "Nessuna", "なし", "없음", "無", "Žádná", "Nincs", "Brak", "Нет", "Geen"}},
{"Look forward", {"Olhar para frente", "Nach vorn schauen", "Regarder devant", "Mirar al frente", "Mirar al frente", "Guarda avanti", "前方を見る", "앞 보기", "向前看", "Pohled dopředu", "Előrenézés", "Spójrz do przodu", "Смотреть вперёд", "Vooruit kijken"}},
{"Controller: A", {"Controle: A", "Controller: A", "Manette : A", "Mando: A", "Control: A", "Controller: A", "コントローラー: A", "컨트롤러: A", "控制器：A", "Ovladač: A", "Kontroller: A", "Kontroler: A", "Контроллер: A", "Controller: A"}},
{"Controller: B", {"Controle: B", "Controller: B", "Manette : B", "Mando: B", "Control: B", "Controller: B", "コントローラー: B", "컨트롤러: B", "控制器：B", "Ovladač: B", "Kontroller: B", "Kontroler: B", "Контроллер: B", "Controller: B"}},
{"Controller: X", {"Controle: X", "Controller: X", "Manette : X", "Mando: X", "Control: X", "Controller: X", "コントローラー: X", "컨트롤러: X", "控制器：X", "Ovladač: X", "Kontroller: X", "Kontroler: X", "Контроллер: X", "Controller: X"}},
{"Controller: Y", {"Controle: Y", "Controller: Y", "Manette : Y", "Mando: Y", "Control: Y", "Controller: Y", "コントローラー: Y", "컨트롤러: Y", "控制器：Y", "Ovladač: Y", "Kontroller: Y", "Kontroler: Y", "Контроллер: Y", "Controller: Y"}},
{"Controller: LB", {"Controle: LB", "Controller: LB", "Manette : LB", "Mando: LB", "Control: LB", "Controller: LB", "コントローラー: LB", "컨트롤러: LB", "控制器：LB", "Ovladač: LB", "Kontroller: LB", "Kontroler: LB", "Контроллер: LB", "Controller: LB"}},
{"Controller: RB", {"Controle: RB", "Controller: RB", "Manette : RB", "Mando: RB", "Control: RB", "Controller: RB", "コントローラー: RB", "컨트롤러: RB", "控制器：RB", "Ovladač: RB", "Kontroller: RB", "Kontroler: RB", "Контроллер: RB", "Controller: RB"}},
{"Controller: LT", {"Controle: LT", "Controller: LT", "Manette : LT", "Mando: LT", "Control: LT", "Controller: LT", "コントローラー: LT", "컨트롤러: LT", "控制器：LT", "Ovladač: LT", "Kontroller: LT", "Kontroler: LT", "Контроллер: LT", "Controller: LT"}},
{"Controller: RT", {"Controle: RT", "Controller: RT", "Manette : RT", "Mando: RT", "Control: RT", "Controller: RT", "コントローラー: RT", "컨트롤러: RT", "控制器：RT", "Ovladač: RT", "Kontroller: RT", "Kontroler: RT", "Контроллер: RT", "Controller: RT"}},
{"Controller: d-pad up", {"Controle: direcional cima", "Controller: Steuerkreuz hoch", "Manette : croix haut", "Mando: cruceta arriba", "Control: cruceta arriba", "Controller: croce su", "コントローラー: 方向パッド上", "컨트롤러: 방향 패드 위", "控制器：方向鍵上", "Ovladač: kříž nahoru", "Kontroller: iránypad fel", "Kontroler: krzyżak w górę", "Контроллер: крестовина вверх", "Controller: D-pad omhoog"}},
{"Controller: d-pad down", {"Controle: direcional baixo", "Controller: Steuerkreuz runter", "Manette : croix bas", "Mando: cruceta abajo", "Control: cruceta abajo", "Controller: croce giù", "コントローラー: 方向パッド下", "컨트롤러: 방향 패드 아래", "控制器：方向鍵下", "Ovladač: kříž dolů", "Kontroller: iránypad le", "Kontroler: krzyżak w dół", "Контроллер: крестовина вниз", "Controller: D-pad omlaag"}},
{"Controller: d-pad left", {"Controle: direcional esquerda", "Controller: Steuerkreuz links", "Manette : croix gauche", "Mando: cruceta izquierda", "Control: cruceta izquierda", "Controller: croce sinistra", "コントローラー: 方向パッド左", "컨트롤러: 방향 패드 왼쪽", "控制器：方向鍵左", "Ovladač: kříž doleva", "Kontroller: iránypad balra", "Kontroler: krzyżak w lewo", "Контроллер: крестовина влево", "Controller: D-pad links"}},
{"Controller: d-pad right", {"Controle: direcional direita", "Controller: Steuerkreuz rechts", "Manette : croix droite", "Mando: cruceta derecha", "Control: cruceta derecha", "Controller: croce destra", "コントローラー: 方向パッド右", "컨트롤러: 방향 패드 오른쪽", "控制器：方向鍵右", "Ovladač: kříž doprava", "Kontroller: iránypad jobbra", "Kontroler: krzyżak w prawo", "Контроллер: крестовина вправо", "Controller: D-pad rechts"}},
{"Controller: left stick up", {"Controle: analógico esq. cima", "Controller: linker Stick hoch", "Manette : stick gauche haut", "Mando: stick izquierdo arriba", "Control: stick izquierdo arriba", "Controller: levetta sinistra su", "コントローラー: 左スティック上", "컨트롤러: 왼쪽 스틱 위", "控制器：左搖桿上", "Ovladač: levá páčka nahoru", "Kontroller: bal kar fel", "Kontroler: lewa gałka w górę", "Контроллер: левый стик вверх", "Controller: linkerstick omhoog"}},
{"Controller: left stick down", {"Controle: analógico esq. baixo", "Controller: linker Stick runter", "Manette : stick gauche bas", "Mando: stick izquierdo abajo", "Control: stick izquierdo abajo", "Controller: levetta sinistra giù", "コントローラー: 左スティック下", "컨트롤러: 왼쪽 스틱 아래", "控制器：左搖桿下", "Ovladač: levá páčka dolů", "Kontroller: bal kar le", "Kontroler: lewa gałka w dół", "Контроллер: левый стик вниз", "Controller: linkerstick omlaag"}},
{"Controller: left stick click", {"Controle: clique analógico esq.", "Controller: linken Stick drücken", "Manette : clic du stick gauche", "Mando: pulsar stick izquierdo", "Control: presionar stick izquierdo", "Controller: clic levetta sinistra", "コントローラー: 左スティック押し込み", "컨트롤러: 왼쪽 스틱 누르기", "控制器：按下左搖桿", "Ovladač: stisk levé páčky", "Kontroller: bal kar lenyomása", "Kontroler: wciśnięcie lewej gałki", "Контроллер: нажатие левого стика", "Controller: linkerstick indrukken"}},
{"Controller: right stick up", {"Controle: analógico dir. cima", "Controller: rechter Stick hoch", "Manette : stick droit haut", "Mando: stick derecho arriba", "Control: stick derecho arriba", "Controller: levetta destra su", "コントローラー: 右スティック上", "컨트롤러: 오른쪽 스틱 위", "控制器：右搖桿上", "Ovladač: pravá páčka nahoru", "Kontroller: jobb kar fel", "Kontroler: prawa gałka w górę", "Контроллер: правый стик вверх", "Controller: rechterstick omhoog"}},
{"Controller: right stick down", {"Controle: analógico dir. baixo", "Controller: rechter Stick runter", "Manette : stick droit bas", "Mando: stick derecho abajo", "Control: stick derecho abajo", "Controller: levetta destra giù", "コントローラー: 右スティック下", "컨트롤러: 오른쪽 스틱 아래", "控制器：右搖桿下", "Ovladač: pravá páčka dolů", "Kontroller: jobb kar le", "Kontroler: prawa gałka w dół", "Контроллер: правый стик вниз", "Controller: rechterstick omlaag"}},
{"Controller: right stick left", {"Controle: analógico dir. esquerda", "Controller: rechter Stick links", "Manette : stick droit gauche", "Mando: stick derecho izquierda", "Control: stick derecho izquierda", "Controller: levetta destra sinistra", "コントローラー: 右スティック左", "컨트롤러: 오른쪽 스틱 왼쪽", "控制器：右搖桿左", "Ovladač: pravá páčka doleva", "Kontroller: jobb kar balra", "Kontroler: prawa gałka w lewo", "Контроллер: правый стик влево", "Controller: rechterstick links"}},
{"Controller: right stick right", {"Controle: analógico dir. direita", "Controller: rechter Stick rechts", "Manette : stick droit droite", "Mando: stick derecho derecha", "Control: stick derecho derecha", "Controller: levetta destra destra", "コントローラー: 右スティック右", "컨트롤러: 오른쪽 스틱 오른쪽", "控制器：右搖桿右", "Ovladač: pravá páčka doprava", "Kontroller: jobb kar jobbra", "Kontroler: prawa gałka w prawo", "Контроллер: правый стик вправо", "Controller: rechterstick rechts"}},
{"Controller: right stick click", {"Controle: clique analógico dir.", "Controller: rechten Stick drücken", "Manette : clic du stick droit", "Mando: pulsar stick derecho", "Control: presionar stick derecho", "Controller: clic levetta destra", "コントローラー: 右スティック押し込み", "컨트롤러: 오른쪽 스틱 누르기", "控制器：按下右搖桿", "Ovladač: stisk pravé páčky", "Kontroller: jobb kar lenyomása", "Kontroler: wciśnięcie prawej gałki", "Контроллер: нажатие правого стика", "Controller: rechterstick indrukken"}},
};
// clang-format on

inline uint8_t* g_base = nullptr;
inline std::wstring g_ini;
inline std::atomic<int> g_saved{-2};   // the language last seen (an index, -1 English), from the ini; -2 unread

// Czech and Hungarian: the game shows its own text in English there (their string tables are
// English), and its fonts there may not have their letters. (The translations are kept for
// when that changes.)
inline bool english_there(int i) { return !std::strcmp(kCodes[i], "CZ") || !std::strcmp(kCodes[i], "HU"); }

// The game's text language: an index into kCodes, -1 English, -2 not known yet.
inline int game_language() {
    if (!g_base) return -2;
    __try {
        auto* manager = *reinterpret_cast<uint8_t**>(g_base + RVA_STRINGS);
        if (!manager) return -2;
        const uint8_t* name = manager + 0x30;
        std::size_t count, capacity;
        std::memcpy(&count, name + 16, sizeof count);
        std::memcpy(&capacity, name + 24, sizeof capacity);
        if (capacity != 15 || count < 2 || count > 3 || name[count]) return -2;
        for (int i = 0; i < kLangs; ++i)
            if (!_stricmp(reinterpret_cast<const char*>(name), kCodes[i])) return english_there(i) ? -1 : i;
        return -1;   // EN, GB, DEV, LOC
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -2;
    }
}

inline int language() {
    const int now = game_language();
    if (now == -2) return g_saved.load() < -1 ? -1 : g_saved.load();
    if (now != g_saved.exchange(now) && !g_ini.empty()) {   // for the rows next time
        wchar_t code[8] = L"EN";
        if (now >= 0) MultiByteToWideChar(CP_UTF8, 0, kCodes[now], -1, code, 8);
        WritePrivateProfileStringW(L"Language", L"Last", code, g_ini.c_str());
    }
    return now;
}

// The ini the language seen last is kept in: before tr() is used.
inline void init(const std::wstring& ini) {
    g_ini           = ini;
    wchar_t code[8] = {};
    GetPrivateProfileStringW(L"Language", L"Last", L"EN", code, 8, ini.c_str());
    char narrow[8] = {};
    WideCharToMultiByte(CP_UTF8, 0, code, -1, narrow, 8, nullptr, nullptr);
    int saved = -1;
    for (int i = 0; i < kLangs; ++i)
        if (!_stricmp(narrow, kCodes[i]) && !english_there(i)) saved = i;
    g_saved = saved;
}

// The game's image: from then on the language is the game's own.
inline void set_base(uint8_t* base) { g_base = base; }

// `english` in the game's language now; itself when there's no translation.
inline const char* tr(const char* english) {
    if (!english) return english;
    const int l = language();
    if (l < 0) return english;
    for (const Text& t : kTexts)
        if (!std::strcmp(t.en, english)) return t.t[l];
    return english;
}

} // namespace lang
