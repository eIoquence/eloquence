# Atmosphere — Design Document

**Proiect:** Eloquence Exokernel x86-64  
**Componentă:** GUI Desktop (track separat, faza finală)  
**Versiune:** 0.1  
**Status:** Proiect / Preliminar  
**Data:** 06.11.2026

---

## 1. Vizion general

Atmosphere este un desktop environment minimal, modern și eficient, construit explicit pentru exokernelul **Eloquence**. Nu este un port al unui desktop existent (GNOME, KDE, etc.) — este scris de la zero, cu accent pe:

- **Memorie sub 150 MB în stare de idle** (măsurată efectiv, nu teoretic)
- **Fără dependințe desktop grele** (fără Qt, GTK, Electron, Wayland compositor extern)
- **C++20 pentru nucleu** (compositor, window manager, toolkit UI)
- **Rust / Zig pentru securitate** (sandbox aplicații, verificare capabilități, izolare memorie)
- **Drivers în C** (kernel-level, compatibil cu baza de cod existentă `eloquence`)

Interfața visată (conform poza furnizată):
- Bar de stat sus-stânga (ceas, dată)
- Launcher / search bar central ("Start typing to search")
- Panou lateral stânga cu aplicații (Install, Chrome, Figma — demo)
- Panou de setări rapide în colțul stânga-jos (Wi-Fi, sunet, lumină)
- Notificări
- Estetica: fundal întunecat cu glasuri, panouri blur, borduri rotunjite, contrast înalt

---

## 2. Arhitectura de nivel înalt

```
┌─────────────────────────────────────────────────────────────┐
│                     Atmosphere Desktop                      │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────────────┐   │
│  │ Launcher│ │ Settings│ │  Notif. │ │   Apps (Chrome,   │   │
│  │  Shell  │ │  Panel  │ │  Center │ │   Figma, etc.)  │   │
│  └─────────┘ └─────────┘ └─────────┘ └─────────────────┘   │
│         ▲           ▲           ▲            ▲            │
│         └───────────┴───────────┴────────────────┘        │
│                   UI Toolkit (C++20)                        │
│         ▲           ▲           ▲            ▲            │
│         └───────────┴───────────┴────────────────┘        │
│                   Compositor / WM (C++20)                   │
│         ▲           ▲           ▲            ▲            │
│         └───────────┴───────────┴────────────────┘        │
│               GFX Syscalls (kernel: framebuffer)            │
│         ▲           ▲           ▲            ▲            │
│         └───────────┴───────────┴────────────────┘        │
│                Kernel: fb.c, input.c, syscalls 72-78        │
│         ▲           ▲           ▲            ▲            │
│         └───────────┴───────────┴────────────────┘        │
│               Hardware: VGA / VirtIO-GPU / Real GPU         │
└─────────────────────────────────────────────────────────────┘
```

**Separația de răsculă:**
- **Kernel (C):** framebuffer, tastatură PS/2, mouse, syscalls GFX + INPUT. Nu știe nimic despre "ferețe" sau "butoane".
- **Compositor (C++20):** primește evenimente de input, tratează ferețele, trimită cadre pe framebuffer.
- **UI Toolkit (C++20):** componente vizuale (butoane, câmpuri, liste, panouri).
- **Security Services (Rust/Zig):** sandbox aplicații, verificare capabilități GUI, izolare memorie.

---

## 3. Componente detaliate

### 3.1 Kernel (C) — deja parțial implementat

**Fișiere existente:**
- `kernel/drivers/video/fb.{c,h}` — framebuffer din Limine
- `kernel/arch/x86_64/syscall.c` — syscalls 72-78 (GFX_GET_INFO, DRAW_PIXEL, FILL_RECT, CLEAR, FLIP, INPUT_GET, INPUT_POLL)

**De adăugat:**
- `kernel/drivers/input/ps2.{c,h}` — tastatură + mouse PS/2 (QEMU emulează implicit)
- `kernel/drivers/input/input_queue.{c,h}` — coadă de evenimente (taste, click, mișcare mouse)
- Syscall `INPUT_GET` returnează un eveniment; `INPUT_POLL` verifică dacă există

**Format eveniment input (propriu):**
```c
struct input_event {
    uint64_t timestamp_ms;
    uint8_t  type;      // 0 = key_down, 1 = key_up, 2 = mouse_move, 3 = mouse_btn_down, 4 = mouse_btn_up
    uint8_t  key_code;  // scancode sau buton mouse
    int32_t  x, y;      // poziție mouse sau 0 pentru tastatură
    int32_t  dx, dy;    // mișcare relativă mouse
};
```

### 3.2 Compositor / Window Manager (C++20)

**Rol:** singurul proces care scrie pe framebuffer. Gestionează stiva de ferețe, focus, redesenări.

**Fișiere propuse:**
- `userspace/atmosphere/compositor.cpp`
- `userspace/atmosphere/window.cpp / window.h`
- `userspace/atmosphere/wm.cpp / wm.h`

**Starea unei ferestre:**
```cpp
enum class WindowState : uint8_t {
    Closed,
    Minimized,
    Maximized,
    Normal
};

struct Window {
    uint32_t id;
    int32_t x, y, w, h;
    WindowState state;
    bool focused;
    std::vector<uint32_t> backbuffer; // RGBA32
    std::string title;
};
```

**Render loop:**
1. Așteaptă eveniment input (din kernel via `INPUT_GET`)
2. Routează evenimentul către fereastra cu focus
3. Redesenă ferețele (de jos în sus) pe un buffer intermediu
4. Copiază buffer-ul pe framebuffer kernel via `GFX_FILL_RECT` sau `GFX_DRAW_PIXEL` (batched)

### 3.3 UI Toolkit (C++20)

**Scop:** componente reutilizabile, fără dependințe externe grele. Biblioteci acceptate (fără framework-uri desktop):
- `stb_truetype` — rendering fonturi (header-only)
- `stb_image_write` — salvare imagini (opțional)

**Componente:**
- `Button` — clickable, hover, pressed state
- `Label` — text static
- `TextField` — input text cu cursor
- `Panel` — container cu background și bordură
- `List` — listă scrollabilă de elemente
- `Checkbox`, `Toggle`

**Stiluri (hardcoded inițial, temă externă ulterior):**
- Fundal panouri: `#1a1a2e` (întunecat)
- Text: `#e0e0e0`
- Accent: `#4cc9f0` (albastru)
- Borduri: rotunjite, 4-8 px radius

### 3.4 Shell / Launcher

**Componente:**
- Bar de căutare central ("Start typing to search")
- Rezultate rapide (Install, Chrome, Figma — demo)
- Click pe rezultat lansează proces prin `spawn`/`exec` (syscall-uri existente)

**Fișiere propuse:**
- `userspace/atmosphere/launcher.cpp`
- `userspace/atmosphere/launcher.h`

### 3.5 Panou de Setări Rapide

**Componente:**
- Wi-Fi (conectare, deconectare — reusează stiva de rețea existentă: `net-stat`, `net-connect`)
- Sunet (volume slider)
- Lumină (brightness slider)
- Reîncarcare / Închidere (folosește `SYSTEM_CONTROL` cu capabilitate `system`)

### 3.6 Notificări

- Coadă de mesaje scurte afișate în colțul stânga-jos
- Timout automat (5 secunde)
- Click pentru închidere

### 3.7 Cursor Mouse

- Rendrat de compositor pe nivelul cel de sus
- Mișcat de evenimente `mouse_move` din kernel
- Imagine cursor: săgeată simplă (24x24 px, embuțată sau generată programat)

---

## 4. API-uri și interfețe

### 4.1 Syscalls GFX (există, 72-78)

| Syscall | Argument 0 | Argument 1 | Argument 2 | Rezultat |
|---------|-----------|-----------|-----------|----------|
| `GFX_GET_INFO` | ptr `framebuffer_info` | - | - | OK / INVALID |
| `GFX_DRAW_PIXEL` | x | y | color (RRGGBB) | OK / INVALID |
| `GFX_FILL_RECT` | x | y | w | h (r10), color (r8) | OK / INVALID |
| `GFX_CLEAR` | color | - | - | OK / INVALID |
| `GFX_FLIP` | - | - | - | OK / INVALID |
| `INPUT_GET` | - | - | - | `input_event` / WOULD_BLOCK |
| `INPUT_POLL` | - | - | - | count / WOULD_BLOCK |

### 4.2 Compozitor → Kernel

Compozițorul nu scrie pixel individual (prea lent). Folosește `GFX_FILL_RECT` pentru blocuri de culoare și `GFX_DRAW_PIXEL` doar pentru detaliu fino (text, iconițe mici).

### 4.3 Aplicații → Compozitor

Aplicațiile GUI nu desenează direct pe framebuffer. Trimiteți o cerere de redesenare prin **IPC pipe** (syscalls 30-32 deja existente) sau prin memorie partajată (faza 2). Compozițorul decide ce și când se redenează.

---

## 5. Buget de memorie (target < 150 MB idle)

| Componentă | Memorie estimată | Observații |
|------------|-----------------|------------|
| Kernel + drivere (framebuffer, input, rețea, capabilități) | 24–32 MB | Includes page tables, framebuffer mapping |
| Compositor + Window Manager | 24–36 MB | C++20, minimal STL |
| UI Toolkit (fonturi, paletă, componente) | 16–24 MB | Fonturi bitmap, fără fonturi externe mari |
| Shell / Launcher | 16–24 MB | Câmp de căutare, iconițe simplificate |
| Panou Setări + Notificări | 8–16 MB | Reutilizează stiva de rețea existentă |
| **Total estimat** | **~88–132 MB** | **Sub 150 MB target** |

**Strategii de memorie:**
- Fonturi: doar glyph-urile necesare (ASCII + diacritice românești), nu întregul font
- Imagini: PNG încărcate la cerere, nu toate în memorie
- Ferețe inactive: backbuffer poate fi eliberat sau comprimat
- Nu rulează daemon-uri neutilizate

---

## 6. Model de securitate (Rust / Zig)

**Rolul securității:**
- Aplicațiile GUI nu au acces direct la framebuffer
- Compozițorul verifică capabilități (`CAP_GUI_WRITE`) înainte de a permite desenarea
- Sandbox pentru aplicații neîncredătoare (nu pot citi tastatura altor procese)

**Implementare:**
- **Rust:** sandbox aplicatie, verificare capabilități, criptografie notificări
- **Zig:** utilitare de memorie, allocator custom pentru UI (opțional, faza ulterioră)

**Interfață kernel → security service:**
- Syscall-uri existente `CAP_QUERY` / `CAP_GRANT` / `CAP_REVOKE` (33-35)
- `CAP_GUI_WRITE` — nou bit de capabilitate (0x20)

---

## 7. Structură de fișiere (propusă)

```
eloquence-work/
├── kernel/
│   ├── drivers/
│   │   ├── video/
│   │   │   ├── fb.h          # EXISTENT
│   │   │   └── fb.c          # EXISTENT
│   │   └── input/
│   │       ├── ps2.h         # NOU
│   │       ├── ps2.c         # NOU
│   │       ├── input_queue.h # NOU
│   │       └── input_queue.c # NOU
│   ├── net/
│   ├── ipc/
│   ├── domain/
│   └── ...
├── include/
│   └── eloquence/
│       └── syscall_abi.h     # EXISTENT (syscalls 72-78 adăugați)
├── userspace/
│   └── atmosphere/
│       ├── compositor/
│       │   ├── compositor.cpp
│       │   ├── window.cpp
│       │   ├── window.h
│       │   ├── wm.cpp
│       │   └── wm.h
│       ├── toolkit/
│       │   ├── button.cpp
│       │   ├── button.h
│       │   ├── label.cpp
│       │   ├── label.h
│       │   ├── panel.cpp
│       │   ├── panel.h
│       │   ├── textfield.cpp
│       │   └── textfield.h
│       ├── shell/
│       │   ├── launcher.cpp
│       │   └── launcher.h
│       ├── settings/
│       │   ├── quick_settings.cpp
│       │   └── quick_settings.h
│       ├── security/         # Rust / Zig (faza ulterioră)
│       │   └── sandbox.rs
│       ├── resources/
│       │   ├── fonts/        # fonturi bitmap
│       │   └── icons/        # pictograme simplificate
│       └── main.cpp          # punct de intrare Atmosphere
├── tools/
│   └── create-qemu-disk.py   # EXISTENT (embed-uri shell.elf, ipc_demo.elf)
├── Makefile                  # EXISTENT (adaugă obiecte atmosphere)
└── DESIGN.md                 # acest document
```

---

## 8. Build system

- Kernel: `Makefile` existent (Clang, ld.lld)
- Userspace C++20: `clang++ -std=c++20 -ffreestanding -nostdinc -Iinclude ...`
- Rust: `cargo` (faza ulterioră, doar security service)
- Linkare: toate binarele userland sunt ELF x86-64, bootate de pe disk FAT32 prin `spawn`/`exec`

**Dependențe externe permise:**
- `stb_truetype.h` (single header, public domain)
- `stb_image.h` (opțional, pentru imagini)

---

## 9. Harta rutierală (fază fază)

| Fază | Obiectiv | Criteriu de finalizare |
|------|----------|------------------------|
| **Faza 1** (acum) | Framebuffer + GFX syscalls | ✅ Făcut — `fb_init`, `GFX_*` funcționează în QEMU |
| **Faza 2** | Driver tastatură/mouse PS/2 + `INPUT_GET` | Tastele apar în shell, cursor se mișcă |
| **Faza 3** | Librarie grafică C++20 (`put_pixel`, `fill_rect`, `draw_text`) | Poate desena un text pe ecran |
| **Faza 4** | Compositor minimal (o fereță, focus, deplasare) | Fereță vizibilă, pot să o mut cu mouse-ul |
| **Faza 5** | Desktop + panou de setări + launcher | Search bar funcțional, lansează o aplicație demo |
| **Faza 6** | Securitate Rust/Zig (sandbox, capabilități GUI) | Aplicație neîncredătoare nu poate scrie pe framebuffer al altui proces |

---

## 10. Deciziuri închis (do-not-revisit)

- **Nu se folosește Qt/GTK/Electron** — prea greu, depinde de biblioteci externe mari
- **Nu se scrie un display server separat (Wayland/X11)** — compositorul e și serverul de afișare
- **Nu se folosește OpenGL/Vulkan în faza 1** — software rendering pe framebuffer, suficient pentru desktop 2D
- **Nu se rulează aplicații Linux nativ** — eload format ELF propriu, nu ELF Linux (nu există syscall compatibility layer)

---

## 11. Întrebări deschise (pentru sesiunea viitoare)

1. Vrei să folosești `stb_truetype` pentru text sau fonturi bitmap simple (8x8, 8x16)?
2. Care este prima aplicație "reală" pe care vrei să rulezi în Atmosphere? (terminal, editor text, browser minimal?)
3. Vrei Rust pentru sandbox acum sau doar C++20 pentru toate componentele, adăugând Rust ulterior?

---

*Acest document este un proiect alive. Se actualizează la sfârșitul fiecărei faze.*
