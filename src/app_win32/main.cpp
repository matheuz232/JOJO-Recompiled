#ifdef _WIN32
#define NOMINMAX
#include "core/conversion.h"
#include "core/runtime.h"
#include <windows.h>
#include <knownfolders.h>
#include <shobjidl.h>
#include <shlobj_core.h>
#include <algorithm>
#include <atomic>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

namespace {
namespace fs = std::filesystem;
constexpr UINT WM_PROGRESS = WM_APP + 10;
constexpr UINT WM_FINISHED = WM_APP + 11;
constexpr int ID_PATH = 1001, ID_SELECT = 1002, ID_PREPARE = 1003;
constexpr COLORREF BG=RGB(13,8,22), PANEL=RGB(35,21,53), TEXT=RGB(248,244,252), MUTED=RGB(185,169,198);
constexpr COLORREF PURPLE=RGB(119,73,196), MAGENTA=RGB(220,64,166), GOLD=RGB(235,193,83);
HWND win{}, path_box{}, select_btn{}, prepare_btn{};
HFONT title_font{}, body_font{}, small_font{}, button_font{};
HBRUSH edit_brush{};
fs::path game_dir;
std::wstring source, status=L"Selecione a imagem da sua própria cópia do jogo.";
std::deque<std::wstring> logs;
int percent=0;
bool running=false, converted=false;
std::atomic_bool closing{false};
struct ProgressMsg { jojo::ConversionProgress p; };
struct FinishMsg { jojo::Result<jojo::ConversionManifest> r; };

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(n<=0) return L"[UTF-8 inválido]";
    std::wstring out(static_cast<size_t>(n),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n);
    return out;
}

fs::path app_root() {
    PWSTR raw=nullptr;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_CREATE,nullptr,&raw)))
        return fs::current_path()/L"JOJO Recompiled User Data";
    fs::path p(raw); CoTaskMemFree(raw); return p/L"JOJO Recompiled";
}

void add_log(std::wstring s) {
    logs.push_back(std::move(s));
    while(logs.size()>5) logs.pop_front();
}

void draw_text(HDC dc,const std::wstring& text,RECT r,HFONT font,COLORREF color,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE){
    auto old=SelectObject(dc,font); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,color);
    DrawTextW(dc,text.c_str(),-1,&r,flags); SelectObject(dc,old);
}

void fill_round(HDC dc,RECT r,COLORREF c,int radius=14){
    HBRUSH b=CreateSolidBrush(c); HPEN p=CreatePen(PS_SOLID,1,c);
    auto ob=SelectObject(dc,b), op=SelectObject(dc,p); RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);
    SelectObject(dc,op); SelectObject(dc,ob); DeleteObject(p); DeleteObject(b);
}

void paint(HDC dc,RECT c){
    HBRUSH b=CreateSolidBrush(BG); FillRect(dc,&c,b); DeleteObject(b);
    POINT a[]={{c.right-390,0},{c.right,0},{c.right,210},{c.right-510,118}};
    b=CreateSolidBrush(RGB(48,25,73)); auto old=SelectObject(dc,b); Polygon(dc,a,4); SelectObject(dc,old); DeleteObject(b);
    for(int x=c.right-300;x<c.right;x+=38){ HPEN p=CreatePen(PS_SOLID,2,RGB(82,50,98)); auto op=SelectObject(dc,p); MoveToEx(dc,x,15,nullptr); LineTo(dc,x+120,145); SelectObject(dc,op); DeleteObject(p); }

    draw_text(dc,L"JOJO RECOMPILED",{78,45,800,100},title_font,TEXT);
    draw_text(dc,L"HERITAGE FOR THE FUTURE  •  PROJETO NATIVO WINDOWS",{82,102,820,136},body_font,GOLD);
    draw_text(dc,L"Use uma imagem obtida da sua própria cópia. O projeto não distribui ROM, arte, música ou dados do jogo.",{82,155,905,215},body_font,MUTED,DT_LEFT|DT_TOP|DT_WORDBREAK);
    draw_text(dc,L"IMAGEM DA SUA CÓPIA",{82,246,500,280},body_font,TEXT);
    draw_text(dc,status,{82,356,815,402},body_font,converted?GOLD:MUTED,DT_LEFT|DT_TOP|DT_WORDBREAK);
    draw_text(dc,std::to_wstring(percent)+L"%",{820,365,920,400},body_font,GOLD,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);

    RECT track{80,405,920,438}; fill_round(dc,track,RGB(34,21,47));
    RECT bar{84,409,84+(832*std::clamp(percent,0,100))/100,434};
    if(bar.right>bar.left){ b=CreateSolidBrush(MAGENTA); FillRect(dc,&bar,b); DeleteObject(b); }

    RECT card{80,474,920,620}; fill_round(dc,card,PANEL);
    draw_text(dc,L"ATIVIDADE",{102,487,400,518},body_font,GOLD);
    int y=520; for(const auto& l:logs){ draw_text(dc,l,{102,y,892,y+21},small_font,MUTED); y+=19; }
    draw_text(dc,L"Dados convertidos: %LOCALAPPDATA%\\JOJO Recompiled",{80,646,600,690},small_font,RGB(144,128,155),DT_LEFT|DT_TOP|DT_WORDBREAK);
}

std::wstring choose_image(){
    IFileOpenDialog* d=nullptr; if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&d)))) return {};
    const COMDLG_FILTERSPEC f[]={{L"Imagens suportadas",L"*.iso;*.bin;*.cue;*.gdi"},{L"Todos os arquivos",L"*.*"}};
    d->SetFileTypes(2,f); d->SetTitle(L"Selecione a imagem da sua própria cópia"); std::wstring out;
    if(SUCCEEDED(d->Show(win))){ IShellItem* item=nullptr; if(SUCCEEDED(d->GetResult(&item))){ PWSTR p=nullptr; if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&p))){out=p;CoTaskMemFree(p);} item->Release(); }}
    d->Release(); return out;
}

void set_enabled(bool on){EnableWindow(select_btn,on);EnableWindow(prepare_btn,on);}

void refresh_install(){
    auto i=jojo::validate_installation(game_dir); converted=static_cast<bool>(i);
    if(!i){percent=0;status=L"Selecione a imagem da sua própria cópia do jogo.";add_log(L"Aguardando preparação inicial.");SetWindowTextW(prepare_btn,L"PREPARAR JOGO");return;}
    percent=100; SetWindowTextW(prepare_btn,L"REFAZER PREPARAÇÃO");
    if(i.value.manifest.backend=="native-ready"){
        status=L"Instalação nativa pronta.";
        add_log(L"Backend nativo detectado.");
    }else if(i.value.manifest.backend=="psx-runtime-prepared"){
        status=L"Runtime PS1 preparado localmente. Boot comercial ainda em desenvolvimento.";
        add_log(L"Boot PS1 e trilha de dados preparados; R2 ainda não foi concluído.");
    }else{
        status=L"Preparação base detectada. Backend específico do jogo ainda pendente.";
        add_log(L"Instalação convertida encontrada.");
    }
}

void start_conversion(){
    if(running) return; if(source.empty()){status=L"Selecione uma imagem .ISO, .BIN, .CUE ou .GDI.";add_log(L"Nenhuma imagem selecionada.");InvalidateRect(win,nullptr,FALSE);return;}
    running=true;converted=false;percent=0;logs.clear();status=L"Iniciando preparação...";add_log(L"Processo iniciado.");set_enabled(false);InvalidateRect(win,nullptr,FALSE);
    const std::wstring src=source; const fs::path dest=game_dir; const HWND target=win;
    std::thread([src,dest,target](){
        std::error_code ec;fs::create_directories(dest/L"logs",ec);std::ofstream log(dest/L"logs"/L"conversion.log",std::ios::trunc);
        auto r=jojo::convert_image(fs::path(src),dest,[&](const jojo::ConversionProgress& p){
            if(log){log<<p.percent<<"% ["<<p.message_key<<"] "<<p.detail<<'\n';log.flush();}
            if(closing.load()) return; auto* m=new ProgressMsg{p}; if(!PostMessageW(target,WM_PROGRESS,0,reinterpret_cast<LPARAM>(m))) delete m;
        });
        if(log&&!r) log<<"ERROR: "<<r.detail<<'\n'; if(closing.load()) return; auto* m=new FinishMsg{std::move(r)}; if(!PostMessageW(target,WM_FINISHED,0,reinterpret_cast<LPARAM>(m))) delete m;
    }).detach();
}

void make_fonts(){
    title_font=CreateFontW(-42,0,0,0,FW_HEAVY,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI Black");
    body_font=CreateFontW(-19,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    small_font=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    button_font=body_font;
}

void create_controls(){
    path_box=CreateWindowExW(0,L"EDIT",L"Nenhuma imagem selecionada",WS_CHILD|WS_VISIBLE|ES_READONLY|ES_AUTOHSCROLL,82,286,616,42,win,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PATH)),GetModuleHandleW(nullptr),nullptr);
    select_btn=CreateWindowExW(0,L"BUTTON",L"SELECIONAR",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,712,286,208,42,win,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SELECT)),GetModuleHandleW(nullptr),nullptr);
    prepare_btn=CreateWindowExW(0,L"BUTTON",L"PREPARAR JOGO",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,640,644,280,48,win,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PREPARE)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(path_box,WM_SETFONT,reinterpret_cast<WPARAM>(body_font),TRUE);
}

void draw_button(DRAWITEMSTRUCT* d){
    const bool off=(d->itemState&ODS_DISABLED)!=0, press=(d->itemState&ODS_SELECTED)!=0; COLORREF c=d->CtlID==ID_PREPARE?MAGENTA:PURPLE;
    if(press)c=RGB(GetRValue(c)*3/4,GetGValue(c)*3/4,GetBValue(c)*3/4);if(off)c=RGB(68,54,76);fill_round(d->hDC,d->rcItem,c);
    wchar_t t[64]{};GetWindowTextW(d->hwndItem,t,64);draw_text(d->hDC,t,d->rcItem,button_font,off?MUTED:TEXT,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}

LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_CREATE:create_controls();refresh_install();return 0;
    case WM_COMMAND:
        if(LOWORD(w)==ID_SELECT){auto p=choose_image();if(!p.empty()){source=p;SetWindowTextW(path_box,p.c_str());status=L"Imagem selecionada. Pronto para preparar.";add_log(L"Imagem selecionada.");InvalidateRect(h,nullptr,FALSE);}return 0;}
        if(LOWORD(w)==ID_PREPARE){start_conversion();return 0;}break;
    case WM_PROGRESS:{std::unique_ptr<ProgressMsg> p(reinterpret_cast<ProgressMsg*>(l));if(p){percent=std::clamp(p->p.percent,0,100);status=wide(p->p.detail);add_log(L"["+std::to_wstring(percent)+L"%] "+wide(p->p.detail));InvalidateRect(h,nullptr,FALSE);}return 0;}
    case WM_FINISHED:{std::unique_ptr<FinishMsg> p(reinterpret_cast<FinishMsg*>(l));running=false;set_enabled(true);if(!p||!p->r){status=L"Falha na preparação."+(p?L" "+wide(p->r.detail):L"");if(p)add_log(L"ERRO: "+wide(p->r.detail));}else{percent=100;converted=true;SetWindowTextW(prepare_btn,L"REFAZER PREPARAÇÃO");auto r=jojo::bootstrap_runtime(game_dir);if(!r&&r.error==jojo::ErrorCode::backend_unavailable){status=L"Preparação base concluída. O backend nativo é o próximo marco.";add_log(L"Conversão base concluída.");}else if(!r){status=L"Validação do runtime falhou: "+wide(r.detail);}else if(p->r.value.backend=="native-ready"){status=L"Instalação nativa pronta.";add_log(L"Backend nativo validado.");}else if(p->r.value.backend=="psx-runtime-prepared"){status=L"Runtime PS1 preparado localmente. Boot comercial ainda em desenvolvimento.";add_log(L"Preparação PS1 validada; R2 ainda requer boot comercial real.");}else{status=L"Preparação concluída, mas o backend final ainda não está pronto.";}}InvalidateRect(h,nullptr,FALSE);return 0;}
    case WM_DRAWITEM:draw_button(reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;
    case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:{HDC dc=reinterpret_cast<HDC>(w);SetTextColor(dc,TEXT);SetBkColor(dc,PANEL);return reinterpret_cast<INT_PTR>(edit_brush);}
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps{};HDC dc=BeginPaint(h,&ps);RECT c{};GetClientRect(h,&c);paint(dc,c);EndPaint(h,&ps);return 0;}
    case WM_CLOSE:closing.store(true);DestroyWindow(h);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }return DefWindowProcW(h,m,w,l);
}
}

int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,PWSTR,int show){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 2;
    game_dir=app_root()/L"game";std::error_code ec;fs::create_directories(game_dir.parent_path(),ec);make_fonts();edit_brush=CreateSolidBrush(PANEL);
    WNDCLASSEXW c{};c.cbSize=sizeof(c);c.lpfnWndProc=proc;c.hInstance=inst;c.hCursor=LoadCursorW(nullptr,IDC_ARROW);c.hIcon=LoadIconW(nullptr,IDI_APPLICATION);c.lpszClassName=L"JOJORecompiledWindow";
    if(!RegisterClassExW(&c)){CoUninitialize();return 3;}win=CreateWindowExW(0,c.lpszClassName,L"JOJO Recompiled",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,1018,758,nullptr,nullptr,inst,nullptr);
    if(!win){CoUninitialize();return 4;}ShowWindow(win,show);UpdateWindow(win);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    if(title_font)DeleteObject(title_font);if(body_font)DeleteObject(body_font);if(small_font)DeleteObject(small_font);if(edit_brush)DeleteObject(edit_brush);CoUninitialize();return static_cast<int>(msg.wParam);
}
#endif
