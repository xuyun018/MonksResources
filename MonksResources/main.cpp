#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>

#include "resource.h"

#pragma comment(lib, "comctl32.lib")

// ====== 把任意尺寸 HBITMAP 变成 48x48 缩略图 ======
static HBITMAP MakeThumb48_FromHBITMAP(HBITMAP src, COLORREF bgColor, BOOL allowUpscale)
{
	const int TW = 48, TH = 48;
	if (!src) return NULL;

	BITMAP bm{};
	if (GetObject(src, sizeof(bm), &bm) != sizeof(bm)) return NULL;
	int srcW = bm.bmWidth, srcH = bm.bmHeight;
	if (srcW <= 0 || srcH <= 0) return NULL;

	BITMAPINFO bi{};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = TW;
	bi.bmiHeader.biHeight = -TH; // top-down
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;

	void* bits = NULL;
	HDC screen = GetDC(NULL);
	HBITMAP dst = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
	ReleaseDC(NULL, screen);
	if (!dst) return NULL;

	HDC dcDst = CreateCompatibleDC(NULL);
	HDC dcSrc = CreateCompatibleDC(NULL);
	HGDIOBJ oldDst = SelectObject(dcDst, dst);
	HGDIOBJ oldSrc = SelectObject(dcSrc, src);

	// 背景填充（也是 mask 色）
	HBRUSH br = CreateSolidBrush(bgColor);
	RECT rc{ 0,0,TW,TH };
	FillRect(dcDst, &rc, br);
	DeleteObject(br);

	// 等比缩放居中
	double sx = (double)TW / (double)srcW;
	double sy = (double)TH / (double)srcH;
	double s = (sx < sy) ? sx : sy;
	if (!allowUpscale && s > 1.0) s = 1.0;

	int drawW = (int)(srcW * s + 0.5);
	int drawH = (int)(srcH * s + 0.5);
	if (drawW < 1) drawW = 1;
	if (drawH < 1) drawH = 1;

	int offX = (TW - drawW) / 2;
	int offY = (TH - drawH) / 2;

	// 大图缩小：HALFTONE 更好看
	if (srcW > TW * 2 || srcH > TH * 2) {
		SetStretchBltMode(dcDst, HALFTONE);
		SetBrushOrgEx(dcDst, 0, 0, NULL);
	}
	else {
		SetStretchBltMode(dcDst, COLORONCOLOR);
	}

	StretchBlt(dcDst, offX, offY, drawW, drawH, dcSrc, 0, 0, srcW, srcH, SRCCOPY);

	SelectObject(dcSrc, oldSrc);
	SelectObject(dcDst, oldDst);
	DeleteDC(dcSrc);
	DeleteDC(dcDst);

	return dst;
}

// 载入位图资源，缩放到48x48，然后 ImageList_AddMasked，返回 image index（失败返回 -1）
static int AddBmpResToImageList48(HINSTANCE hInst, HIMAGELIST himg, int resId, COLORREF maskColor, BOOL allowUpscale)
{
	HBITMAP hbmp = (HBITMAP)LoadImageW(
		hInst, MAKEINTRESOURCEW(resId),
		IMAGE_BITMAP, 0, 0,
		LR_CREATEDIBSECTION
	);
	if (!hbmp) return -1;

	HBITMAP thumb = MakeThumb48_FromHBITMAP(hbmp, maskColor, allowUpscale);
	DeleteObject(hbmp);
	if (!thumb) return -1;

	int idx = ImageList_AddMasked(himg, thumb, maskColor);
	DeleteObject(thumb);

	return idx; // 关键：这个 idx 就是 ListView item 要用的 iImage
}

// ====== 全局 UI ======
static HWND g_hList = NULL;
static HIMAGELIST g_hImgSmall = NULL;

static void SetupListViewColumns(HWND hList)
{
	LVCOLUMNW col{};
	col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

	col.pszText = (LPWSTR)L"Name";
	col.cx = 320;
	col.iSubItem = 0;
	ListView_InsertColumn(hList, 0, &col);

	col.pszText = (LPWSTR)L"ImageIndex";
	col.cx = 110;
	col.iSubItem = 1;
	ListView_InsertColumn(hList, 1, &col);

	col.pszText = (LPWSTR)L"ResID";
	col.cx = 80;
	col.iSubItem = 2;
	ListView_InsertColumn(hList, 2, &col);
}

static void PopulateListView(HWND hList, HINSTANCE hInst)
{
	// 48x48 的小图列表（Report 模式下也能当 small icon 用）
	const int TW = 48, TH = 48;
	const COLORREF mask = RGB(255, 0, 255);
	const BOOL allowUpscale = FALSE;

	if (g_hImgSmall) { ImageList_Destroy(g_hImgSmall); g_hImgSmall = NULL; }
	g_hImgSmall = ImageList_Create(TW, TH, ILC_COLOR32 | ILC_MASK, 0, 64);

	// 绑定到 ListView（小图标）
	ListView_SetImageList(hList, g_hImgSmall, LVSIL_SMALL);

	ListView_DeleteAllItems(hList);

	int j = IDI_MAIN_ICON;
	for (int i = 0; i < IDBS_COUNT; i++) {

		// 1) 加入 ImageList，拿到 image index
		int imgIndex = AddBmpResToImageList48(hInst, g_hImgSmall, ++j, mask, allowUpscale);

		WCHAR name[10];
		_itow(j, name, 10);

		// 2) 插入 ListView item，并把 iImage=imgIndex
		LVITEMW item{};
		item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
		item.iItem = i;
		item.iSubItem = 0;
		item.pszText = (LPWSTR)name;
		item.iImage = (imgIndex >= 0) ? imgIndex : 0;
		item.lParam = (LPARAM)j; // lParam 里存 resId，后面想用可直接取

		int row = ListView_InsertItem(hList, &item);

		// 3) 其它列：显示 index / resId
		wchar_t buf[64];
		wsprintfW(buf, L"%d", imgIndex);
		ListView_SetItemText(hList, row, 1, buf);

		wsprintfW(buf, L"%d", j);
		ListView_SetItemText(hList, row, 2, buf);
	}
}

// ====== Win32 window ======
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_CREATE: {
		RECT rc; GetClientRect(hwnd, &rc);

		g_hList = CreateWindowExW(
			WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
			WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
			0, 0, rc.right - rc.left, rc.bottom - rc.top,
			hwnd, (HMENU)1001, GetModuleHandleW(NULL), NULL);

		ListView_SetExtendedListViewStyle(g_hList,
			LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);

		SetupListViewColumns(g_hList);
		PopulateListView(g_hList, GetModuleHandleW(NULL));
		return 0;
	}
	case WM_SIZE: {
		if (g_hList) {
			MoveWindow(g_hList, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
		}
		return 0;
	}
	case WM_NOTIFY: {
		NMHDR* hdr = (NMHDR*)lParam;
		if (hdr->hwndFrom == g_hList && hdr->code == NM_DBLCLK) {
			int sel = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
			if (sel >= 0) {
				LVITEMW it{};
				it.mask = LVIF_IMAGE | LVIF_PARAM;
				it.iItem = sel;
				ListView_GetItem(g_hList, &it);

				wchar_t buf[128];
				wsprintfW(buf, L"Row=%d  ImageIndex=%d  ResID(lParam)=%d", sel, it.iImage, (int)it.lParam);
				MessageBoxW(hwnd, buf, L"Item Mapping", MB_OK);
			}
		}
		return 0;
	}
	case WM_DESTROY:
		if (g_hImgSmall) { ImageList_Destroy(g_hImgSmall); g_hImgSmall = NULL; }
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nShow)
{
	INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES };
	InitCommonControlsEx(&icc);

	WNDCLASSW wc{};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInst;
	wc.lpszClassName = L"ImgListListViewDemo";
	wc.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_MAIN_ICON));
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	RegisterClassW(&wc);

	HWND hwnd = CreateWindowExW(
		0, wc.lpszClassName, L"ImageList <-> ListView Item Mapping Demo (48x48)",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, 820, 520,
		NULL, NULL, hInst, NULL);

	ShowWindow(hwnd, nShow);
	UpdateWindow(hwnd);

	MSG msg;
	while (GetMessageW(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	return 0;
}
