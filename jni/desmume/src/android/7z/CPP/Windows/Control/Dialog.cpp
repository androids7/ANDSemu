// Dialog.cpp

#include "StdAfx.h"

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"
 
#ifdef __BORLANDC__
    #pragma hdrstop
#endif

// for all others, include the necessary headers (this file is usually all you
// need because it includes almost all "standard" wxWidgets headers)
#ifndef WX_PRECOMP
    #include "wx/wx.h"
#endif  

#include "Windows/Control/DialogImpl.h"
#include "Windows/Synchronization.h"

// #include "../GUI/p7zip_32.xpm"
extern const char * p7zip_32_xpm[];

const TCHAR * nameWindowToUnix(const TCHAR * lpFileName) {
  if ((lpFileName[0] == wxT('c')) && (lpFileName[1] == wxT(':'))) return lpFileName+2;
  return lpFileName;
}


extern time_t g_T0; // FIXME

extern wxWindow * g_window;

enum { // FIXME - duplicate
    WORKER_EVENT=100    // this one gets sent from the worker thread
};

#define DIALOG_ID_MESSAGEBOX  8100
#define DIALOG_ID_DIR_DIALOG  8101
#define DIALOG_ID_FILE_DIALOG 8102
#define DIALOG_ID_POST_DIALOG 8190
#define DIALOG_ID_END_DIALOG  8199

static struct
{
	bool busy;

	int id;
	wxWindow *parentWindow;

	// CreateDialog
	NWindows::NControl::CModalDialog * dialog;

	// EndModal
	int value;
	NWindows::NControl::CModalDialogImpl * window;

	// MessageBox
	const TCHAR * msg;
	const TCHAR * title;
	int flag;

	// 
	LPCWSTR initialFolderOrFile;

	wxSemaphore * sem;
	int ret;

	UString resultPath;
	
#define MAX_CREATE 16
} g_tabCreate[MAX_CREATE];

static int myCreateHandle2(int n);

static int findFreeInd()
{
static NWindows::NSynchronization::CCriticalSection g_CriticalSection;

	g_CriticalSection.Enter();
	int ind = 0;
	while (ind < MAX_CREATE)
	{
		if (g_tabCreate[ind].busy == false)
		{
			g_tabCreate[ind].busy = true;
			break;
		}
		ind++;
	}
	g_CriticalSection.Leave();

	return ind;
}

static int WaitInd(int ind,int id,wxWindow * parent,UString &resultPath)
{
	int ret = 0;

	g_tabCreate[ind].id           = id;
	g_tabCreate[ind].parentWindow = parent;
	g_tabCreate[ind].ret          = 0;
	g_tabCreate[ind].resultPath   = wxEmptyString;

	if (wxThread::IsMain())
	{
		ret = myCreateHandle2(ind);
		resultPath = g_tabCreate[ind].resultPath;
	}
	else
	{
		g_tabCreate[ind].sem = new wxSemaphore(0);

		// create any type of command event here
		wxCommandEvent event( wxEVT_COMMAND_MENU_SELECTED, WORKER_EVENT );
		event.SetInt( ind );

		// send in a thread-safe way
		// DEBUG printf("T=0x%lx - %d : WaitInd(%d,%p): BEGIN\n", wxThread::GetCurrentId(),time(0)-g_T0,g_tabCreate[ind].id,g_tabCreate[ind].parentWindow);
		wxPostEvent( g_window, event );

		g_tabCreate[ind].sem->Wait();

		ret = g_tabCreate[ind].ret;
		resultPath = g_tabCreate[ind].resultPath;
		// DEBUG printf("T=0x%lx - %d : WaitInd(%d,%p): ret=%d\n", wxThread::GetCurrentId(),time(0)-g_T0,g_tabCreate[ind].id,g_tabCreate[ind].parentWindow,ret);
		delete g_tabCreate[ind].sem;
		g_tabCreate[ind].sem = 0;
	}

	g_tabCreate[ind].busy = false;

	return ret;
}

static int WaitInd(int ind,int id,wxWindow * parent)
{
	UString resultPath;
	return WaitInd(ind,id,parent,resultPath);
}

class LockGUI
{
	bool _IsMain;
	public:
		LockGUI() { 
			_IsMain = wxThread::IsMain();
			if (!_IsMain) {
				// DEBUG printf("GuiEnter(0x%lx)\n",wxThread::GetCurrentId());
				wxMutexGuiEnter();
			}
	       	}
		~LockGUI() { 
			if (!_IsMain) {
				wxMutexGuiLeave();
				// DEBUG printf("GuiLeave(0x%lx)\n",wxThread::GetCurrentId());
			}
	       	}
};

static const unsigned int kNumDialogsMax = 32;
static unsigned int g_NumDialogs = 0;
static const CDialogInfo *g_Dialogs[kNumDialogsMax]; 

void RegisterDialog(const CDialogInfo *dialogInfo) 
{ 
  // DEBUG printf("RegisterDialog : %d\n",dialogInfo->id);
  if (g_NumDialogs < kNumDialogsMax)
    g_Dialogs[g_NumDialogs++] = dialogInfo; 
}

namespace NWindows {

	CSysString MyLoadString(unsigned int resourceID)
	{
		for(int i=0; i < g_NumDialogs; i++) {
			if (g_Dialogs[i]->stringTable) {
				int j = 0;
				while(g_Dialogs[i]->stringTable[j].str) {
					if (resourceID == g_Dialogs[i]->stringTable[j].id) {
						return g_Dialogs[i]->stringTable[j].str;
					}

					j++;
				}
			}
		}
		return L"FIXME-MyLoadStringW-";
	}

	namespace NControl {

/////////////////////////////////////////// CModalDialog //////////////////////////////////

			bool CModalDialog::CheckButton(int buttonID, UINT checkState)
			{
				LockGUI lock;
				wxCheckBox* w = (wxCheckBox*)_window->FindWindow(buttonID);
				if (w)
				{
					w->SetValue(checkState == BST_CHECKED);
					return true;
				}
				return false;
			}

			UINT CModalDialog::IsButtonChecked(int buttonID) const
			{ 
				LockGUI lock;
				wxCheckBox* w = (wxCheckBox*)_window->FindWindow(buttonID);
				if (w)
				{
					bool bret = w->GetValue();
					if (bret) return BST_CHECKED;
				}
				return BST_UNCHECKED;
			}

			void CModalDialog::EnableItem(int id, bool state)
			{
				LockGUI lock;
				wxWindow* w = _window->FindWindow(id);
				if (w) w->Enable(state);
			}

			void CModalDialog::SetItemText(int id, const TCHAR *txt)
			{
				LockGUI lock;
				wxWindow* w = _window->FindWindow(id);
				if (w)
				{
					wxString label(txt);
					w->SetLabel(label);
				}
			}

			wxWindow * CModalDialog::GetItem(long id) const
			{
				LockGUI lock;
				return _window->FindWindow(id);
			}

			void CModalDialog::ShowItem(int itemID, int cmdShow) const
			{
				LockGUI lock;
				// cmdShow = SW_HIDE or SW_SHOW (sometimes false or true !)
				wxWindow* w = _window->FindWindow(itemID);
				if (w)
				{
// FIXME					w->Show(cmdShow != SW_HIDE);
					w->Enable(cmdShow != SW_HIDE);
				}
			}

			UINT_PTR CModalDialog::SetTimer(UINT_PTR idEvent , unsigned milliseconds)
			{
				LockGUI lock;
				return _window->SetTimer(idEvent , milliseconds);
			}

			void CModalDialog::KillTimer(UINT_PTR idEvent)
			{
				LockGUI lock;
				_window->KillTimer(idEvent);
			}

			void CModalDialog::SetText(const TCHAR *_title) {
				LockGUI lock;
			      	_window->SetTitle(_title);
		       	}


			bool CModalDialog::GetText(CSysString &s) {
				wxString str;
				{
					LockGUI lock;
	  				str = _window->GetTitle();
				}
	  			s = str;
	  			return true;
		       	}

			INT_PTR CModalDialog::Create(int id , HWND parentWindow)
			{
				int ind = findFreeInd();

				g_tabCreate[ind].dialog = this;

				return WaitInd(ind,id,parentWindow);
			}

			void CModalDialog::End(int result)
			{ 
				int ind = findFreeInd();

				g_tabCreate[ind].window  = _window;
				g_tabCreate[ind].value   = result;

				WaitInd(ind,DIALOG_ID_END_DIALOG,0);
			}

			void CModalDialog::PostMessage(UINT message)
			{
				int ind = findFreeInd();

				g_tabCreate[ind].dialog  = this;
				g_tabCreate[ind].value   = message;

				WaitInd(ind,DIALOG_ID_POST_DIALOG,0);
			}

/////////////////////////////////////////// CModalDialogImpl ///////////////////////////////////////

			CModalDialogImpl::CModalDialogImpl(CDialog *dialog, wxWindow* parent, wxWindowID id, 
					 const wxString& title, const wxPoint& pos,
					 const wxSize& size, long style) :
			   		wxDialog(parent, id, title , pos , size, style /* | wxDIALOG_NO_PARENT */ ) ,
				       	_timer(this, TIMER_ID_IMPL), _dialog(dialog)
			{
				// set the frame icon
				this->SetIcon(wxICON(p7zip_32));
			}

			void CModalDialogImpl::OnAnyButton(wxCommandEvent& event)
			{
				int id = event.GetId();
				if (id == wxID_OK)
				{
					if (_dialog) _dialog->OnOK();
					// event.Skip(true);
				}
				else if (id == wxID_CANCEL)
				{
					if (_dialog) _dialog->OnCancel();
					// event.Skip(true);
				}
				else if (id == wxID_HELP)
				{
					if (_dialog) _dialog->OnHelp();
				}
				else
				{
					if (_dialog)
					{
						bool bret = _dialog->OnButtonClicked(id, FindWindow(id) );
					}
				}
			}

			void CModalDialogImpl::OnAnyChoice(wxCommandEvent &event)
			{
				int itemID =  event.GetId();
				if (_dialog) _dialog->OnCommand(CBN_SELCHANGE, itemID, 0);
			}

			void CModalDialogImpl::OnAnyTimer(wxTimerEvent &event)
			{
				int timerID =  event.GetId();
				if (_dialog) _dialog->OnTimer(timerID , 0);
			}
	}
}

///////////////////////// myCreateHandle


static int myCreateHandle2(int n)
{ 
	int                                    id           = g_tabCreate[n].id;
	wxWindow *                             parentWindow = g_tabCreate[n].parentWindow;
	NWindows::NControl::CModalDialogImpl * window       = 0;

	// DEBUG printf("T=0x%lx - %d : myCreateHandle(%d): BEGIN\n", wxThread::GetCurrentId(),time(0)-g_T0,n);

	if (id == DIALOG_ID_END_DIALOG)
	{
		/* FIXME : the dialog must be shown before ending it ?
		while (!_window->IsShownOnScreen()) Sleep(200);
		Sleep(200);
		*/
		g_tabCreate[n].window->EndModal(g_tabCreate[n].value);
		return 0;
	}

	if (id == DIALOG_ID_POST_DIALOG)
	{
		g_tabCreate[n].dialog->OnMessage(g_tabCreate[n].value, 0, 0);
		return 0;
	}

	if (id == DIALOG_ID_MESSAGEBOX)
	{
		long style = g_tabCreate[n].flag;
		long decorated_style = style;
		if ( ( style & ( wxICON_EXCLAMATION | wxICON_HAND | wxICON_INFORMATION |
				wxICON_QUESTION ) ) == 0 )
		{
			decorated_style |= ( style & wxYES ) ? wxICON_QUESTION : wxICON_INFORMATION ;
		}
		wxMessageDialog dialog(parentWindow, g_tabCreate[n].msg, g_tabCreate[n].title, decorated_style);
		dialog.SetIcon(wxICON(p7zip_32));
		int ret = dialog.ShowModal();

		return ret;
	}

	if (id == DIALOG_ID_DIR_DIALOG)
	{
		wxString defaultDir = g_tabCreate[n].initialFolderOrFile;
		wxDirDialog dirDialog(g_tabCreate[n].parentWindow,
			       	g_tabCreate[n].title, defaultDir);
		dirDialog.SetIcon(wxICON(p7zip_32));
		int ret = dirDialog.ShowModal();
		if (ret == wxID_OK) g_tabCreate[n].resultPath = dirDialog.GetPath();
		return ret;
	}

	if (id == DIALOG_ID_FILE_DIALOG)
	{
		wxString defaultFilename = g_tabCreate[n].initialFolderOrFile;
		wxFileDialog fileDialog(g_tabCreate[n].parentWindow, g_tabCreate[n].title,
				wxEmptyString, defaultFilename,
		       		wxT("All Files (*.*)|*.*"), wxSAVE|wxOVERWRITE_PROMPT);
		fileDialog.SetIcon(wxICON(p7zip_32));
		int ret = fileDialog.ShowModal();
		if (ret == wxID_OK) g_tabCreate[n].resultPath = fileDialog.GetPath();
		return ret;
	}

	for(int i=0; i < g_NumDialogs; i++) {
		if (id == g_Dialogs[i]->id) {
			// DEBUG printf("%d : Create(%d,%p): CreateDialog-1\n",time(0)-g_T0,id,parentWindow);
			window = (g_Dialogs[i]->createDialog)(g_tabCreate[n].dialog,g_tabCreate[n].parentWindow);
			// DEBUG printf("%d : Create(%d,%p): CreateDialog-2\n",time(0)-g_T0,id,parentWindow);
			break;
		}
	}

	if (window) {

		// DEBUG printf("%d : Create(%d,%p): %p->ShowModal()\n",time(0)-g_T0,id,parentWindow,window);

		INT_PTR ret = window->ShowModal();

		// DEBUG printf("%d : Create(%d,%p): %p->ShowModal() - ret=%d\n",time(0)-g_T0,id,parentWindow,window,ret);
		window->Detach();
		window->Destroy();

		// DEBUG printf("%d : Create(%d,%p): END\n",time(0)-g_T0,id,parentWindow,window);

		return ret;
	}

	// FIXME
	printf("INTERNAL ERROR : cannot find dialog %d\n",id);

	return 0;
}

void myCreateHandle(int n)
{
	int ret = myCreateHandle2(n);
	g_tabCreate[n].ret = ret;
	g_tabCreate[n].sem->Post();
}

int MessageBoxW(wxWindow * parent, const TCHAR * msg, const TCHAR * title,int flag)
{
	int ind = findFreeInd();

	g_tabCreate[ind].msg          = msg;
	g_tabCreate[ind].title        = title;
	g_tabCreate[ind].flag         = flag;
	
	return WaitInd(ind,DIALOG_ID_MESSAGEBOX,parent);
}



// FIXME : should be in Windows/Shell.cpp

namespace NWindows{
namespace NShell{

bool BrowseForFolder(HWND owner, LPCWSTR title, LPCWSTR initialFolder, UString &resultPath)
{
	int ind = findFreeInd();

	g_tabCreate[ind].title               = title;
	g_tabCreate[ind].initialFolderOrFile = nameWindowToUnix(initialFolder);
	
	UString resTmp;
	int ret = WaitInd(ind,DIALOG_ID_DIR_DIALOG,owner,resTmp);
	if(ret == wxID_OK)
	{
		resultPath = resTmp;
		return true;
	}
	return false;
}

}}

/////////////////////////// CPP/Windows/CommonDialog.cpp
namespace NWindows
{

	bool MyGetOpenFileName(HWND hwnd, LPCWSTR title, LPCWSTR fullFileName, LPCWSTR s, UString &resPath)
	{
		int ind = findFreeInd();

		g_tabCreate[ind].title               = title;
		g_tabCreate[ind].initialFolderOrFile = nameWindowToUnix(fullFileName);
	
		UString resTmp;
		int ret = WaitInd(ind,DIALOG_ID_FILE_DIALOG,hwnd,resTmp);
		if(ret == wxID_OK)
		{
			resPath = resTmp;
			return true;
		}
		return false;
	}
}

