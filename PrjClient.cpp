#define _CRT_SECURE_NO_WARNINGS         // ??? VC++ ?????? ?? ??? ????
#define _WINSOCK_DEPRECATED_NO_WARNINGS // ??? VC++ ?????? ?? ??? ????
#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include "resource.h"

#define SERVERIPV4  "127.0.0.1"
#define SERVERIPV6  "::1"
#define SERVERPORT  9000

#define BUFSIZE     256                    // ???? ????? ??? ???
#define MSGSIZE     (BUFSIZE-sizeof(int))  // ??? ????? ??? ????

#define CHATTING    1000                   // ????? ???: ???
#define DRAWLINE    1001                   // ????? ???: ?? ?????
#define DRAWREC     1002
#define DRAWCIR     1003
#define DRAWTRI     1004
#define PERMITTION  1005
#define DRAWREC     1002                   // 메시지 타입: 직사각형 그리기
#define DRAWCIR     1003                   // 메시지 타입: 원 그리기
#define DRAWTRI     1004                   // 메시지 타입: 삼각형 그리기

#define WM_DRAWIT   (WM_USER+1)            // ????? ???? ?????? ?????

// ???? ????? ????
// sizeof(COMM_MSG) == 256
struct COMM_MSG
{
    int  type;
    char dummy[MSGSIZE];
};

// ??? ????? ????
// sizeof(CHAT_MSG) == 256
struct CHAT_MSG
{
    int  type;
    char buf[MSGSIZE];
};

//Permission Msg
struct PERMISSION_MSG {
    int type;
    char buf[MSGSIZE];
};

// ?? ????? ????? ????
// sizeof(DRAWLINE_MSG) == 256
struct DRAW_MSG
{
    int  type;
    int  color;
    int  x0, y0;
    int  x1, y1;
    char dummy[BUFSIZE - 6 * sizeof(int)];
};


static HINSTANCE     g_hInst; // ???? ???α?? ?ν???? ???
static HWND          g_hDrawWnd; // ????? ??? ??????
static HWND          g_hButtonSendMsg; // '????? ????' ???
static HWND          g_hEditStatus; // ???? ????? ???
static char          g_ipaddr[64]; // ???? IP ???
static u_short       g_port; // ???? ??? ???
static BOOL          g_isIPv6; // IPv4 or IPv6 ????
static HANDLE        g_hClientThread; // ?????? ???
static volatile BOOL g_bStart; // ??? ???? ????
static SOCKET        g_sock; // ??????? ????
static HANDLE        g_hReadEvent, g_hWriteEvent; // ???? ???
static CHAT_MSG      g_chatmsg; // ??? ????? ????
static DRAW_MSG  g_drawmsg; // ?? ????? ????? ????
static int           g_drawcolor; // ?? ????? ????


// ??????? ???ν???
BOOL CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
// ???? ??? ?????? ???
DWORD WINAPI ClientMain(LPVOID arg);
DWORD WINAPI ReadThread(LPVOID arg);
DWORD WINAPI WriteThread(LPVOID arg);
// ??? ?????? ???ν???
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
// ???? ????? ??? ???
void DisplayText(char *fmt, ...);
// ????? ???? ?????? ???? ???
int recvn(SOCKET s, char *buf, int len, int flags);
// ???? ??? ???
void err_quit(char *msg);
void err_display(char *msg);

// ???? ???
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
    LPSTR lpCmdLine, int nCmdShow)
{
    // ???? ????
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

    // ???? ????
    g_hReadEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
    if (g_hReadEvent == NULL) return 1;
    g_hWriteEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (g_hWriteEvent == NULL) return 1;

    // ???? ????(???)
    g_chatmsg.type = CHATTING;
    g_drawmsg.type = DRAWLINE;
    g_drawmsg.color = RGB(255, 0, 0);
   
    // ??????? ????
    g_hInst = hInstance;
    DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, DlgProc);

    // ???? ????
    CloseHandle(g_hReadEvent);
    CloseHandle(g_hWriteEvent);

    // ???? ????
    WSACleanup();
    return 0;
}

// ??????? ???ν???
BOOL CALLBACK DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static HWND hButtonIsIPv6;
    static HWND hEditIPaddr;
    static HWND hEditPort;
    static HWND hButtonConnect;
    static HWND hEditMsg;
    static HWND hColorRed;
    static HWND hColorGreen;
    static HWND hColorBlue;

    switch (uMsg) {
    case WM_INITDIALOG:
        // ????? ??? ???
        hButtonIsIPv6 = GetDlgItem(hDlg, IDC_ISIPV6);
        hEditIPaddr = GetDlgItem(hDlg, IDC_IPADDR);
        hEditPort = GetDlgItem(hDlg, IDC_PORT);
        hButtonConnect = GetDlgItem(hDlg, IDC_CONNECT);
        g_hButtonSendMsg = GetDlgItem(hDlg, IDC_SENDMSG);
        hEditMsg = GetDlgItem(hDlg, IDC_MSG);
        g_hEditStatus = GetDlgItem(hDlg, IDC_STATUS);
        hColorRed = GetDlgItem(hDlg, IDC_COLORRED);
        hColorGreen = GetDlgItem(hDlg, IDC_COLORGREEN);
        hColorBlue = GetDlgItem(hDlg, IDC_COLORBLUE);

        // ????? ????
        SendMessage(hEditMsg, EM_SETLIMITTEXT, MSGSIZE, 0);
        EnableWindow(g_hButtonSendMsg, FALSE);
        SetDlgItemText(hDlg, IDC_IPADDR, SERVERIPV4);
        SetDlgItemInt(hDlg, IDC_PORT, SERVERPORT, FALSE);
        SendMessage(hColorRed, BM_SETCHECK, BST_CHECKED, 0);
        SendMessage(hColorGreen, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessage(hColorBlue, BM_SETCHECK, BST_UNCHECKED, 0);

        // ?????? ????? ???
        WNDCLASS wndclass;
        wndclass.style = CS_HREDRAW | CS_VREDRAW;
        wndclass.lpfnWndProc = WndProc;
        wndclass.cbClsExtra = 0;
        wndclass.cbWndExtra = 0;
        wndclass.hInstance = g_hInst;
        wndclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
        wndclass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
        wndclass.lpszMenuName = NULL;
        wndclass.lpszClassName = "MyWndClass";
        if (!RegisterClass(&wndclass)) return 1;

        // ??? ?????? ????
        g_hDrawWnd = CreateWindow("MyWndClass", "??? ??? ??????", WS_CHILD,
            450, 38, 425, 415, hDlg, (HMENU)NULL, g_hInst, NULL);
        if (g_hDrawWnd == NULL) return 1;
        ShowWindow(g_hDrawWnd, SW_SHOW);
        UpdateWindow(g_hDrawWnd);

        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_ISIPV6:
            //IPv6 ???? ???? ?? ??????? Case
            g_isIPv6 = SendMessage(hButtonIsIPv6, BM_GETCHECK, 0, 0);
            if (g_isIPv6 == false)
                SetDlgItemText(hDlg, IDC_IPADDR, SERVERIPV4);
            else
                SetDlgItemText(hDlg, IDC_IPADDR, SERVERIPV6);
            return TRUE;
            

        case IDC_CONNECT:
            //???? ??? Case
            GetDlgItemText(hDlg, IDC_IPADDR, g_ipaddr, sizeof(g_ipaddr));
            g_port = GetDlgItemInt(hDlg, IDC_PORT, NULL, FALSE);
            g_isIPv6 = SendMessage(hButtonIsIPv6, BM_GETCHECK, 0, 0);

            // ???? ??? ?????? ????
            g_hClientThread = CreateThread(NULL, 0, ClientMain, NULL, 0, NULL);
            //???? ???? ????? ??? ????
            if (g_hClientThread == NULL) {
                MessageBox(hDlg, "????????? ?????? ?? ???????."
                    "\r\n???α???? ????????.", "????!", MB_ICONERROR);
                EndDialog(hDlg, 0);
            }
            else {
                EnableWindow(hButtonConnect, FALSE);
                while (g_bStart == FALSE); // ???? ???? ???? ????
                EnableWindow(hButtonIsIPv6, FALSE);
                EnableWindow(hEditIPaddr, FALSE);
                EnableWindow(hEditPort, FALSE);
                EnableWindow(g_hButtonSendMsg, TRUE);
                SetFocus(hEditMsg);
            }
            return TRUE;
        case IDC_LOGIN:
            // 입력 된 ID와 PW를 받아온다
            // g_PerMSG에 넣어서 보낸다
            // 리턴에 따라서 그걸 처리한다....? -> 어떻게 돌려받을 것인가?
            //ID PW를 일단 보낸다 -> 서버에 저장된 놈과 다르다면 뱉어냄
            /*WaitForSingleObject(g_hReadEvent, INFINITE);
            GetDlgItemText(hDlg, IDC_MSG, g_chatmsg.buf, MSGSIZE);
            // 쓰기 완료를 알림
            SetEvent(g_hWriteEvent);
             //입력된 텍스트 전체를 선택 표시
            SendMessage(hEditMsg, EM_SETSEL, 0, -1);*/
            return TRUE;
        case IDC_SENDMSG:
            //?????? ??? case
            // ?б? ??? ????
            WaitForSingleObject(g_hReadEvent, INFINITE);
            GetDlgItemText(hDlg, IDC_MSG, g_chatmsg.buf, MSGSIZE);
            // ???? ??? ???
            SetEvent(g_hWriteEvent);
            // ???? ???? ????? ???? ???
            SendMessage(hEditMsg, EM_SETSEL, 0, -1);
            return TRUE;

        case IDC_COLORRED:
            //?????? ??? case
            g_drawmsg.color = RGB(255, 0, 0);
            return TRUE;

        case IDC_COLORGREEN:
            //???? ??? case
            g_drawmsg.color = RGB(0, 255, 0);
            return TRUE;

        case IDC_COLORBLUE:
            //????? ??? case
            g_drawmsg.color = RGB(0, 0, 255);
            return TRUE;

        case IDC_LINE:
            //?????? ?? ????? -> ?????? ????? ?????? ??
            g_drawmsg.type = DRAWLINE;
            return TRUE;

        case IDC_REC:
            //?????? ?????
            g_drawmsg.type = DRAWREC;
            return TRUE;

        case IDC_CIR:
            //?? ?????
            g_drawmsg.type = DRAWCIR;
            return TRUE;

        case IDC_TRI:
            //???? ?????
            g_drawmsg.type = DRAWTRI;
            return TRUE;

        case IDCANCEL:
            //???? ??? case 
            if (MessageBox(hDlg, "?????? ??????ð??????",
                "????", MB_YESNO | MB_ICONQUESTION) == IDYES)
            {
                closesocket(g_sock);
                EndDialog(hDlg, IDCANCEL);
            }
            return TRUE;

        }
        return FALSE;
    }

    return FALSE;
}

// ???? ??? ?????? ???
DWORD WINAPI ClientMain(LPVOID arg)
{
    int retval;

    if (g_isIPv6 == false) {
        // socket()
        g_sock = socket(AF_INET, SOCK_STREAM, 0);
        //???? ????
        if (g_sock == INVALID_SOCKET) err_quit("socket()");

        /*u_long on = 1;
        retval = ioctlsocket(g_sock, FIONBIO, &on);
        if (retval == SOCKET_ERROR) err_quit("ioctlsocket()");*/
        //????? ???? ???

        // connect()
        SOCKADDR_IN serveraddr;
        ZeroMemory(&serveraddr, sizeof(serveraddr));
        serveraddr.sin_family = AF_INET;
        serveraddr.sin_addr.s_addr = inet_addr(g_ipaddr);
        serveraddr.sin_port = htons(g_port);
        retval = connect(g_sock, (SOCKADDR *)&serveraddr, sizeof(serveraddr));
        if (retval == SOCKET_ERROR) err_quit("connect()");
    }
    else {
        // socket()
        g_sock = socket(AF_INET6, SOCK_STREAM, 0);
        //???? ????
        if (g_sock == INVALID_SOCKET) err_quit("socket()");

        /*u_long on = 1;
        retval = ioctlsocket(g_sock, FIONBIO, &on);
        if (retval == SOCKET_ERROR) err_quit("ioctlsocket()");*/
        //????? ??? ???

        // connect()
        SOCKADDR_IN6 serveraddr;
        ZeroMemory(&serveraddr, sizeof(serveraddr));
        serveraddr.sin6_family = AF_INET6;
        int addrlen = sizeof(serveraddr);
        WSAStringToAddress(g_ipaddr, AF_INET6, NULL,
            (SOCKADDR *)&serveraddr, &addrlen);
        serveraddr.sin6_port = htons(g_port);
        retval = connect(g_sock, (SOCKADDR *)&serveraddr, sizeof(serveraddr));
        if (retval == SOCKET_ERROR) err_quit("connect()");
    }
    MessageBox(NULL, "?????? ??????????.", "????!", MB_ICONINFORMATION);

    // ?б? & ???? ?????? ????
    HANDLE hThread[2];
    hThread[0] = CreateThread(NULL, 0, ReadThread, NULL, 0, NULL);
    hThread[1] = CreateThread(NULL, 0, WriteThread, NULL, 0, NULL);
    if (hThread[0] == NULL || hThread[1] == NULL) {
        MessageBox(NULL, "?????? ?????? ?? ???????."
            "\r\n???α???? ????????.",
            "????!", MB_ICONERROR);
        exit(1);
    }

    g_bStart = TRUE;

    // ?????? ???? ???
    retval = WaitForMultipleObjects(2, hThread, FALSE, INFINITE);
    retval -= WAIT_OBJECT_0;
    if (retval == 0)
        TerminateThread(hThread[1], 1);
    else
        TerminateThread(hThread[0], 1);
    CloseHandle(hThread[0]);
    CloseHandle(hThread[1]);

    g_bStart = FALSE;

    MessageBox(NULL, "?????? ?????? ?????????", "???", MB_ICONINFORMATION);
    EnableWindow(g_hButtonSendMsg, FALSE);

    closesocket(g_sock);
    return 0;
}

// ?????? ???
DWORD WINAPI ReadThread(LPVOID arg)
{
    int retval;
    COMM_MSG comm_msg;
    CHAT_MSG *chat_msg;
    DRAW_MSG *draw_msg;

    while (1) {
        retval = recvn(g_sock, (char *)&comm_msg, BUFSIZE, 0);
        if (retval == 0 || retval == SOCKET_ERROR) {
            break;
        }

        if (comm_msg.type == CHATTING) {
            chat_msg = (CHAT_MSG *)&comm_msg;
            DisplayText("[???? ?????] %s\r\n", chat_msg->buf);
        }

        else if (comm_msg.type == DRAWLINE || comm_msg.type == DRAWREC ||
            comm_msg.type == DRAWTRI || comm_msg.type == DRAWCIR) {
            draw_msg = (DRAW_MSG *)&comm_msg;
            g_drawcolor = draw_msg->color;
            SendMessage(g_hDrawWnd, WM_DRAWIT,
                MAKEWPARAM(draw_msg->x0, draw_msg->y0),
                MAKELPARAM(draw_msg->x1, draw_msg->y1));
        }
    }
    return 0;
}

// ?????? ??????
DWORD WINAPI WriteThread(LPVOID arg)
{
    int retval;

    // ?????? ?????? ???
    while (1) {
        // ???? ??? ??????
        WaitForSingleObject(g_hWriteEvent, INFINITE);

        // ????? ????? 0??? ?????? ????
        if (strlen(g_chatmsg.buf) == 0) {
            // '????? ????' ??? ????
            EnableWindow(g_hButtonSendMsg, TRUE);
            // ?б? ??? ?????
            SetEvent(g_hReadEvent);
            continue;
        }

        // ?????? ??????
        retval = send(g_sock, (char *)&g_chatmsg, BUFSIZE, 0);
        if (retval == SOCKET_ERROR) {
            break;
        }

        // '????? ????' ??? ????
        EnableWindow(g_hButtonSendMsg, TRUE);
        // ?б? ??? ?????
        SetEvent(g_hReadEvent);
    }

    return 0;
}

// ??? ?????? ???ν???
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    HDC hDC;
    int cx, cy;
    PAINTSTRUCT ps;
    RECT rect;
    HPEN hPen, hOldPen;
    static HBITMAP hBitmap;
    static HDC hDCMem;
    static int x0, y0;
    static int x1, y1;
    static BOOL bDrawing = FALSE;

    switch (uMsg) {
    case WM_CREATE:
        hDC = GetDC(hWnd);

        // ????? ?????? ????? ????
        cx = GetDeviceCaps(hDC, HORZRES);
        cy = GetDeviceCaps(hDC, VERTRES);
        hBitmap = CreateCompatibleBitmap(hDC, cx, cy);

        // ??? DC ????
        hDCMem = CreateCompatibleDC(hDC);

        // ????? ???? ?? ??? DC ????? ??????? ???
        SelectObject(hDCMem, hBitmap);
        SelectObject(hDCMem, GetStockObject(WHITE_BRUSH));
        SelectObject(hDCMem, GetStockObject(WHITE_PEN));
        Rectangle(hDCMem, 0, 0, cx, cy);

        ReleaseDC(hWnd, hDC);
        return 0;
    case WM_LBUTTONDOWN:
        //???콺 Left Button ?? ???????? ??
        x0 = LOWORD(lParam);
        y0 = HIWORD(lParam);
        //???? ??? ???
        bDrawing = TRUE;
        //???? ???????? ?????? ?????? BOOL
        return 0;
    case WM_MOUSEMOVE:
        if (bDrawing && g_bStart) {
            //g_bStart?? bDrawing?? ??????????
            if (g_drawmsg.type == DRAWLINE) {
                //???? ????? ???
                x1 = LOWORD(lParam);
                y1 = HIWORD(lParam);

                // ?? ????? ????? ??????
                g_drawmsg.x0 = x0;
                g_drawmsg.y0 = y0;
                g_drawmsg.x1 = x1;
                g_drawmsg.y1 = y1;
                send(g_sock, (char*)&g_drawmsg, BUFSIZE, 0);
                //??????? ???????? drawmsg ???? ??????. 

                x0 = x1;
                y0 = y1;
                //?????? ?????? ???????? ???? ???
                //???? ??????? ???? ???
            }
            else {
                //??? ?????? ????? ???
                x1 = LOWORD(lParam);
                y1 = HIWORD(lParam);

                g_drawmsg.x0 = x0;
                g_drawmsg.y0 = y0;
                
                g_drawmsg.x1 = x1;
                g_drawmsg.y1 = y1;
            }
        }

        return 0;
    case WM_LBUTTONUP:
        //Left Button?? Up ?? ????
        bDrawing = FALSE;
        //Drawing?? False ???
        if (g_drawmsg.type != DRAWLINE) send(g_sock, (char*)&g_drawmsg, BUFSIZE, 0);
        return 0;
    case WM_DRAWIT:
        //????? ???? ????
        hDC = GetDC(hWnd);
        hPen = CreatePen(PS_SOLID, 3, g_drawcolor);
        //wParam -> Point 0, lParam -> Point 1
        if (g_drawmsg.type == DRAWLINE) {
            // ??? ?????
            hOldPen = (HPEN)SelectObject(hDC, hPen);
            MoveToEx(hDC, LOWORD(wParam), HIWORD(wParam), NULL);
            LineTo(hDC, LOWORD(lParam), HIWORD(lParam));
            SelectObject(hDC, hOldPen);

            // ??? ?????? ?????
            hOldPen = (HPEN)SelectObject(hDCMem, hPen);
            MoveToEx(hDCMem, LOWORD(wParam), HIWORD(wParam), NULL);
            LineTo(hDCMem, LOWORD(lParam), HIWORD(lParam));
            SelectObject(hDC, hOldPen);
        }
        else if (g_drawmsg.type == DRAWREC) {
            hOldPen = (HPEN)SelectObject(hDC, hPen);
            Rectangle(hDC, LOWORD(wParam), HIWORD(wParam), LOWORD(lParam), HIWORD(lParam));
            SelectObject(hDC, hOldPen);

            hOldPen = (HPEN)SelectObject(hDCMem, hPen);
            Rectangle(hDCMem, LOWORD(wParam), HIWORD(wParam), LOWORD(lParam), HIWORD(lParam));
            SelectObject(hDC, hOldPen);

        }
        else if (g_drawmsg.type == DRAWCIR) {
            hOldPen = (HPEN)SelectObject(hDC, hPen);
            Ellipse(hDC, LOWORD(wParam), HIWORD(wParam), LOWORD(lParam), HIWORD(lParam));
            SelectObject(hDC, hOldPen);

            hOldPen = (HPEN)SelectObject(hDCMem, hPen);
            Ellipse(hDCMem, LOWORD(wParam), HIWORD(wParam), LOWORD(lParam), HIWORD(lParam));
            SelectObject(hDC, hOldPen);
        }
        else if (g_drawmsg.type == DRAWTRI) {
            
            //wParam, lParam ?? ????? ?????? ??? ??´?. ?????? ????? ???????? ????? ?????? ????
            //???????? ???? ?????? ???? ???? ?м? ?? ???? -> ????? ???? ???? ?????? ?? ???? ????? ?????
            if (HIWORD(wParam) >= HIWORD(lParam)) {
                hOldPen = (HPEN)SelectObject(hDC, hPen);
                MoveToEx(hDC, LOWORD(wParam), HIWORD(wParam), NULL);
                LineTo(hDC, LOWORD(lParam), HIWORD(wParam));
                MoveToEx(hDC, (LOWORD(wParam)+LOWORD(lParam))/2, HIWORD(lParam), NULL);
                LineTo(hDC, LOWORD(wParam), HIWORD(wParam));
                MoveToEx(hDC, (LOWORD(wParam) + LOWORD(lParam)) / 2, HIWORD(lParam), NULL);
                LineTo(hDC, LOWORD(lParam), HIWORD(wParam));
                SelectObject(hDC, hOldPen);


                hOldPen = (HPEN)SelectObject(hDCMem, hPen);
                MoveToEx(hDCMem, LOWORD(wParam), HIWORD(wParam), NULL);
                LineTo(hDCMem, LOWORD(lParam), HIWORD(wParam));
                MoveToEx(hDCMem, (LOWORD(wParam) + LOWORD(lParam)) / 2, HIWORD(lParam), NULL);
                LineTo(hDCMem, LOWORD(wParam), HIWORD(wParam));
                MoveToEx(hDCMem, (LOWORD(wParam) + LOWORD(lParam)) / 2, HIWORD(lParam), NULL);
                LineTo(hDCMem, LOWORD(lParam), HIWORD(wParam));
                SelectObject(hDC, hOldPen);
            }
            else {
                hOldPen = (HPEN)SelectObject(hDC, hPen);
                MoveToEx(hDC, LOWORD(wParam), HIWORD(lParam), NULL);
                LineTo(hDC, LOWORD(lParam), HIWORD(lParam));
                MoveToEx(hDC, (LOWORD(wParam) + LOWORD(lParam)) / 2, HIWORD(wParam), NULL);
                LineTo(hDC, LOWORD(wParam), HIWORD(lParam));
                MoveToEx(hDC, (LOWORD(wParam) + LOWORD(lParam)) / 2, HIWORD(wParam), NULL);
                LineTo(hDC, LOWORD(lParam), HIWORD(lParam));
                SelectObject(hDC, hOldPen);

                hOldPen = (HPEN)SelectObject(hDCMem, hPen);
                MoveToEx(hDCMem, LOWORD(wParam), HIWORD(lParam), NULL);
                LineTo(hDCMem, LOWORD(lParam), HIWORD(lParam));
                MoveToEx(hDCMem, (LOWORD(wParam) + LOWORD(lParam)) / 2, HIWORD(wParam), NULL);
                LineTo(hDCMem, LOWORD(wParam), HIWORD(lParam));
                MoveToEx(hDCMem, (LOWORD(wParam) + LOWORD(lParam)) / 2, HIWORD(wParam), NULL);
                LineTo(hDCMem, LOWORD(lParam), HIWORD(lParam));
                SelectObject(hDCMem, hOldPen);
                SelectObject(hDC, hOldPen);
            }
        }
        DeleteObject(hPen);
        ReleaseDC(hWnd, hDC);
        return 0;
    case WM_PAINT:
        hDC = BeginPaint(hWnd, &ps);

        // ??? ?????? ????? ????? ??? ????
        GetClientRect(hWnd, &rect);
        BitBlt(hDC, 0, 0, rect.right - rect.left,
            rect.bottom - rect.top, hDCMem, 0, 0, SRCCOPY);

        EndPaint(hWnd, &ps);
        return 0;
    case WM_DESTROY:
        DeleteObject(hBitmap);
        DeleteDC(hDCMem);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

// ????? ?????? ????? ???
void DisplayText(char *fmt, ...)
{
    va_list arg;
    va_start(arg, fmt);

    char cbuf[1024];
    vsprintf(cbuf, fmt, arg);

    int nLength = GetWindowTextLength(g_hEditStatus);
    SendMessage(g_hEditStatus, EM_SETSEL, nLength, nLength);
    SendMessage(g_hEditStatus, EM_REPLACESEL, FALSE, (LPARAM)cbuf);

    va_end(arg);
}

// ????? ???? ?????? ???? ???
int recvn(SOCKET s, char *buf, int len, int flags)
{
    int received;
    char *ptr = buf;
    int left = len;

    while (left > 0) {
        received = recv(s, ptr, left, flags);
        if (received == SOCKET_ERROR)
            return SOCKET_ERROR;
        else if (received == 0)
            break;
        left -= received;
        ptr += received;
    }

    return (len - left);
}

// ???? ??? ???? ??? ?? ????
void err_quit(char *msg)
{
    LPVOID lpMsgBuf;
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, WSAGetLastError(),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf, 0, NULL);
    MessageBox(NULL, (LPCTSTR)lpMsgBuf, msg, MB_ICONERROR);
    LocalFree(lpMsgBuf);
    exit(1);
}

// ???? ??? ???? ???
void err_display(char *msg)
{
    LPVOID lpMsgBuf;
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, WSAGetLastError(),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf, 0, NULL);
    printf("[%s] %s", msg, (char *)lpMsgBuf);
    LocalFree(lpMsgBuf);
}
