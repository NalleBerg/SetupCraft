================================================================================
                    SETUPCRAFT ICON SYSTEM - WINDOWS NATIVE ICONS
================================================================================

This project uses Windows system icons from DLL resources. These icons are
available on all Windows systems and provide professional, consistent visuals.

--------------------------------------------------------------------------------
CURRENT ICON USAGE
--------------------------------------------------------------------------------

Icon assignments for buttons:

1. NEW PROJECT BUTTON (➕ Nytt prosjekt)
   Source: imageres.dll, Index: 111
   Description: Green circle with white plus sign

2. OPEN PROJECT BUTTON (📂 Åpne prosjekt)
   Source: shell32.dll, Index: 4
   Description: Open yellow folder icon

3. DELETE PROJECT BUTTON (🗑️ Slett prosjekt)
   Source: shell32.dll, Index: 31
   Description: Recycle bin icon

4. EXIT BUTTON (🚪 Avslutt)
   Source: shell32.dll, Index: 27
   Description: Application/program icon for exit

5. OK BUTTON (✓ OK)
   Source: imageres.dll, Index: 89
   Description: Green circle with white checkmark

6. CANCEL BUTTON (✖ Avbryt)
   Source: shell32.dll, Index: 131
   Description: Red X icon


--------------------------------------------------------------------------------
HOW TO USE SYSTEM ICONS IN CODE
--------------------------------------------------------------------------------

To load an icon from a system DLL:

    wchar_t path[MAX_PATH];
    GetSystemDirectoryW(path, MAX_PATH);
    wcscat(path, L"\\imageres.dll");
    
    HICON hIcon = (HICON)LoadImageW(
        NULL,
        path,
        IMAGE_ICON,
        16, 16,  // Size (16x16 or 32x32)
        LR_LOADFROMFILE
    );

    // OR use ExtractIconW for indexed icons:
    HICON hIcon = ExtractIconW(GetModuleHandle(NULL), path, iconIndex);

To draw the icon:

    DrawIconEx(hdc, x, y, hIcon, 16, 16, 0, NULL, DI_NORMAL);

Remember to destroy icons when done:

    DestroyIcon(hIcon);


--------------------------------------------------------------------------------
FINDING MORE ICONS
--------------------------------------------------------------------------------

Common system DLL icon sources:

1. **shell32.dll** - Classic Windows icons (427 icons in Windows 10)
   Examples:
   - Index 0-2: Generic files and folders
   - Index 3: Closed folder
   - Index 4: Open folder
   - Index 5-7: Floppy disk icons
   - Index 8: Hard drive
   - Index 137: Shield (admin/security)
   - Index 238: Settings gear

2. **imageres.dll** - Modern Vista+ icons (300+ icons)
   Examples:
   - Index 1: Desktop folder
   - Index 2: File/document
   - Index 5: Recycle bin (empty)
   - Index 89: Green checkmark (success)
   - Index 90: Blue info circle
   - Index 93: Yellow warning triangle
   - Index 94: Red X (error)
   - Index 109: Blue plus circle
   - Index 111: Green plus circle
   - Index 161: Shield (UAC)
   - Index 197: Network icon

3. **user32.dll** - Basic UI icons (small collection)
   - Application icon
   - Question mark
   - Warning icon
   - Error icon

4. **moricons.dll** - Additional program icons
   - Various application icons


--------------------------------------------------------------------------------
HOW TO BROWSE ICONS
--------------------------------------------------------------------------------

Method 1: Using Windows Explorer
1. Navigate to C:\Windows\System32\
2. Right-click on shell32.dll or imageres.dll
3. Select "Properties"
4. Some properties dialogs show icon previews

Method 2: Using Resource Hacker (free tool)
1. Download Resource Hacker from angusj.com/resourcehacker
2. Open the DLL file
3. Browse to "Icon Group" section
4. View all icons with their index numbers

Method 3: PowerShell script
Run this to extract all icons to a folder for viewing:

    $dll = "C:\Windows\System32\imageres.dll"
    $outDir = "C:\temp\icons"
    New-Item -ItemType Directory -Force -Path $outDir
    for ($i = 0; $i -lt 300; $i++) {
        # Use a tool like extrac32 or iconextractor utility
    }


--------------------------------------------------------------------------------
ADDING NEW ICONS TO THE PROJECT
--------------------------------------------------------------------------------

1. Choose your icon from a system DLL (use Resource Hacker to browse)

2. In button.h, update ButtonIconInfo structure if needed

3. When creating a button, specify the DLL and icon index:

    CreateCustomButtonWithIcon(
        hwnd,
        IDC_MY_BUTTON,
        L"Button Text",
        ButtonColor::Blue,
        L"imageres.dll",  // DLL name
        111,              // Icon index
        x, y, width, height,
        hInst
    );

4. The icon will be automatically loaded and drawn alongside the text


--------------------------------------------------------------------------------
ICON SIZE RECOMMENDATIONS
--------------------------------------------------------------------------------

- Small icons (16x16): Use for compact buttons, toolbar buttons
- Medium icons (32x32): Use for standard buttons (our default)
- Large icons (48x48): Use for prominent actions, dialogs
- Extra large (256x256): Use for application icons, about boxes

Windows automatically scales icons, but using native size gives best quality.


--------------------------------------------------------------------------------
COLOR AND STYLE GUIDELINES
--------------------------------------------------------------------------------

To maintain consistency with our button color scheme:

- Blue buttons: Use neutral icons (folders, documents, generic actions)
- Green buttons: Use success/add icons (checkmarks, plus signs)
- Red buttons: Use cancel/error icons (X marks, stop signs)

System icons already follow Windows design guidelines, so they'll look
professional and familiar to users.


--------------------------------------------------------------------------------
PERFORMANCE NOTES
--------------------------------------------------------------------------------

- Icons are loaded once at startup and cached in memory
- DrawIconEx is hardware-accelerated on modern Windows
- Icon handles should be destroyed when the application closes
- Loading ~10-20 icons adds negligible startup time (<10ms)


================================================================================
REFERENCE LINKS
================================================================================

Windows Icon Resources:
https://learn.microsoft.com/en-us/windows/win32/menurc/icons

LoadImage API:
https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-loadimagew

ExtractIcon API:
https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-extracticonw

DrawIconEx API:
https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-drawiconex

================================================================================
