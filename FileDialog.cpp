#include "FileDialog.h"

#include <Windows.h>
#include <commdlg.h>

#include "SlipWindow.h"

#include <ShlObj_core.h>

namespace IO
{
    std::string FileDialog::OpenFile(const char* filter)
    {
        OPENFILENAMEA ofn;
        CHAR szFile[260] = { 0 };
        ZeroMemory(&ofn, sizeof(OPENFILENAME));
        ofn.lStructSize = sizeof(OPENFILENAME);
        ofn.hwndOwner = glfwGetWin32Window(&SlipWindow::Get().getWindow());
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = filter;
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameA(&ofn) == TRUE)
        {
            return ofn.lpstrFile;
        }
        return std::string();
    }

    std::string FileDialog::SaveFile(const char* filter)
    {
        OPENFILENAMEA ofn;
        CHAR szFile[260] = { 0 };
        ZeroMemory(&ofn, sizeof(OPENFILENAME));
        ofn.lStructSize = sizeof(OPENFILENAME);
        ofn.hwndOwner = glfwGetWin32Window(&SlipWindow::Get().getWindow());
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = filter;
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetSaveFileNameA(&ofn) == TRUE)
        {
            return ofn.lpstrFile;
        }
        return std::string();
    }
    std::string FileDialog::OpenFolder()
    {
        BROWSEINFO binf = { 0 };
        TCHAR szFolder[260] = { 0 };
        ZeroMemory(&binf, sizeof(BROWSEINFO));
        binf.hwndOwner = glfwGetWin32Window(&SlipWindow::Get().getWindow());

        LPITEMIDLIST pidl = SHBrowseForFolder(&binf);

        if (pidl != NULL)
        {
            if (SHGetPathFromIDList(pidl, szFolder) == TRUE)
            {
                std::wstring output = std::wstring(szFolder);
                std::string str(output.begin(), output.end());
                return str;
            }

            // — Free pidl
            CoTaskMemFree(pidl);
        }
        return std::string();
    }
}