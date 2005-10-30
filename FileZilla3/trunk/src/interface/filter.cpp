#include "FileZilla.h"
#include "filter.h"
#include "filteredit.h"
#include "ipcmutex.h"
#include "filezillaapp.h"
#include "../tinyxml/tinyxml.h"
#include "xmlfunctions.h"
#include <wx/regex.h>
#include "Mainfrm.h"

bool CFilterDialog::m_loaded = false;
std::vector<CFilter> CFilterDialog::m_globalFilters;
std::vector<CFilterSet> CFilterDialog::m_globalFilterSets;

BEGIN_EVENT_TABLE(CFilterDialog, wxDialogEx)
EVT_BUTTON(XRCID("wxID_OK"), CFilterDialog::OnOK)
EVT_BUTTON(XRCID("wxID_CANCEL"), CFilterDialog::OnCancel)
EVT_BUTTON(XRCID("ID_EDIT"), CFilterDialog::OnEdit)
EVT_CHECKLISTBOX(wxID_ANY, CFilterDialog::OnFilterSelect)
END_EVENT_TABLE();

CFilterCondition::CFilterCondition()
{
	type = 0;
	condition = 0;
	pRegEx = 0;
	matchCase = true;
	value = 0;
}

CFilterCondition::~CFilterCondition()
{
	delete pRegEx;
}

CFilterCondition& CFilterCondition::operator=(const CFilterCondition& cond)
{
	type = cond.type;
	condition = cond.condition;
	strValue = cond.strValue;
	value = cond.value;
	matchCase = cond.matchCase;
	delete pRegEx;
	pRegEx = 0;

	return *this;
}

CFilter::CFilter()
{
	matchAll = false;
	filterDirs = true;
	filterFiles = true;

	// Filenames on Windows ignore case
#ifdef __WXMSW__
	matchCase = false;
#else
	matchCase = true;
#endif
}

CFilterDialog::CFilterDialog()
{
	m_pMainFrame = 0;
	m_shiftClick = false;
	
	LoadFilters();
	CompileRegexes();

	if (m_globalFilterSets.empty())
	{
		CFilterSet set;
		set.local.resize(m_filters.size(), false);
		set.remote.resize(m_filters.size(), false);

		m_globalFilterSets.push_back(set);
		m_filterSets.push_back(set);
	}
}

bool CFilterDialog::Create(CMainFrame* parent)
{
	m_pMainFrame = parent;

	if (!Load(parent, _T("ID_FILTER")))
		return false;

	XRCCTRL(*this, "ID_LOCALFILTERS", wxCheckListBox)->Connect(wxID_ANY, wxEVT_LEFT_DOWN, wxMouseEventHandler(CFilterDialog::OnMouseEvent), 0, this);
	XRCCTRL(*this, "ID_LOCALFILTERS", wxCheckListBox)->Connect(wxID_ANY, wxEVT_KEY_DOWN, wxKeyEventHandler(CFilterDialog::OnKeyEvent), 0, this);
	XRCCTRL(*this, "ID_REMOTEFILTERS", wxCheckListBox)->Connect(wxID_ANY, wxEVT_LEFT_DOWN, wxMouseEventHandler(CFilterDialog::OnMouseEvent), 0, this);
	XRCCTRL(*this, "ID_REMOTEFILTERS", wxCheckListBox)->Connect(wxID_ANY, wxEVT_KEY_DOWN, wxKeyEventHandler(CFilterDialog::OnKeyEvent), 0, this);

	DisplayFilters();

	return true;
}

void CFilterDialog::OnOK(wxCommandEvent& event)
{
	m_globalFilters = m_filters;
	CompileRegexes();
	m_globalFilterSets = m_filterSets;

	SaveFilters();
	EndModal(wxID_OK);
}

void CFilterDialog::OnCancel(wxCommandEvent& event)
{
	EndModal(wxID_CANCEL);
}

void CFilterDialog::OnEdit(wxCommandEvent& event)
{
	CFilterEditDialog dlg;
	if (!dlg.Create(this, m_filters, m_filterSets))
		return;
	
	if (dlg.ShowModal() != wxID_OK)
		return;

	m_filters = dlg.GetFilters();
	m_filterSets = dlg.GetFilterSets();

	DisplayFilters();
}

void CFilterDialog::SaveFilters()
{
	CInterProcessMutex(MUTEX_FILTERS);

	wxFileName file(wxGetApp().GetSettingsDir(), _T("filters.xml"));
	TiXmlElement* pDocument = GetXmlFile(file);
	if (!pDocument)
	{
		wxString msg = wxString::Format(_("Could not load \"%s\", please make sure the file is valid and can be accessed.\nAny changes made in the Site Manager could not be saved."), file.GetFullPath().c_str());
		wxMessageBox(msg, _("Error loading xml file"), wxICON_ERROR);

		return;
	}

	TiXmlElement *pFilters = pDocument->FirstChildElement("Filters");
	while (pFilters)
	{
		pDocument->RemoveChild(pFilters);
		pFilters = pDocument->FirstChildElement("Filters");
	}

	pFilters = pDocument->InsertEndChild(TiXmlElement("Filters"))->ToElement();

	for (std::vector<CFilter>::const_iterator iter = m_globalFilters.begin(); iter != m_globalFilters.end(); iter++)
	{
		const CFilter& filter = *iter;
		TiXmlElement* pFilter = pFilters->InsertEndChild(TiXmlElement("Filter"))->ToElement();

		AddTextElement(pFilter, "Name", filter.name);
		AddTextElement(pFilter, "ApplyToFiles", filter.filterFiles ? _T("1") : _T("0"));
		AddTextElement(pFilter, "ApplyToDirs", filter.filterFiles ? _T("1") : _T("0"));
		AddTextElement(pFilter, "MatchType", filter.matchAll ? _T("All") : _T("Any"));
		AddTextElement(pFilter, "MatchCase", filter.matchCase ? _T("1") : _T("0"));

		TiXmlElement* pConditions = pFilter->InsertEndChild(TiXmlElement("Conditions"))->ToElement();
		for (std::vector<CFilterCondition>::const_iterator conditionIter = filter.filters.begin(); conditionIter != filter.filters.end(); conditionIter++)
		{
			const CFilterCondition& condition = *conditionIter;
			TiXmlElement* pCondition = pConditions->InsertEndChild(TiXmlElement("Condition"))->ToElement();

			AddTextElement(pCondition, "Type", wxString::Format(_T("%d"), condition.type));
			AddTextElement(pCondition, "Condition", wxString::Format(_T("%d"), condition.condition));
			AddTextElement(pCondition, "Value", condition.strValue);
		}
	}

	TiXmlElement *pSets = pDocument->FirstChildElement("Sets");
	while (pSets)
	{
		pDocument->RemoveChild(pSets);
		pSets = pDocument->FirstChildElement("Sets");
	}

	pSets = pDocument->InsertEndChild(TiXmlElement("Sets"))->ToElement();

	for (std::vector<CFilterSet>::const_iterator iter = m_globalFilterSets.begin(); iter != m_globalFilterSets.end(); iter++)
	{
		const CFilterSet& set = *iter;
		TiXmlElement* pSet = pSets->InsertEndChild(TiXmlElement("Set"))->ToElement();

		for (unsigned int i = 0; i < set.local.size(); i++)
		{
			TiXmlElement* pItem = pSet->InsertEndChild(TiXmlElement("Item"))->ToElement();
			AddTextElement(pItem, "Local", set.local[i] ? _T("1") : _T("0"));
			AddTextElement(pItem, "Remote", set.remote[i] ? _T("1") : _T("0"));
		}
	}

	SaveXmlFile(file, pDocument);
	delete pDocument->GetDocument();
}

void CFilterDialog::LoadFilters()
{
	if (m_loaded)
	{
		m_filters = m_globalFilters;
		m_filterSets = m_globalFilterSets;
		return;
	}
	m_loaded = true;

	CInterProcessMutex(MUTEX_FILTERS);

	wxFileName file(wxGetApp().GetSettingsDir(), _T("filters.xml"));
	TiXmlElement* pDocument = GetXmlFile(file);
	if (!pDocument)
	{
		wxString msg = wxString::Format(_("Could not load \"%s\", please make sure the file is valid and can be accessed.\nAny changes made in the Site Manager could not be saved."), file.GetFullPath().c_str());
		wxMessageBox(msg, _("Error loading xml file"), wxICON_ERROR);

		return;
	}

	TiXmlElement *pFilters = pDocument->FirstChildElement("Filters");

	if (!pFilters)
	{
		delete pDocument->GetDocument();
		return;
	}

	TiXmlElement *pFilter = pFilters->FirstChildElement("Filter");
	while (pFilter)
	{
		CFilter filter;
		filter.name = GetTextElement(pFilter, "Name");
		if (filter.name == _T(""))
		{
			pFilter = pFilter->NextSiblingElement("Filter");
			continue;
		}

		filter.filterFiles = GetTextElement(pFilter, "ApplyToFiles") == _T("1");
		filter.filterDirs = GetTextElement(pFilter, "ApplyToDirs") == _T("1");

		filter.matchAll = GetTextElement(pFilter, "MatchType") == _T("All");
		filter.matchCase = GetTextElement(pFilter, "MatchCase") == _T("1");

		TiXmlElement *pConditions = pFilter->FirstChildElement("Conditions");
		if (!pConditions)
		{
			pFilter = pFilter->NextSiblingElement("Filter");
			continue;
		}
		
		TiXmlElement *pCondition = pConditions->FirstChildElement("Condition");
		while (pCondition)
		{
			CFilterCondition condition;
			condition.type = GetTextElementInt(pCondition, "Type", 0);
			condition.condition = GetTextElementInt(pCondition, "Condition", 0);
			condition.strValue = GetTextElement(pCondition, "Value");
			condition.matchCase = filter.matchCase;
			if (condition.strValue == _T(""))
			{
				pCondition = pCondition->NextSiblingElement("Condition");
				continue;
			}

			// TODO: 64bit filesize
			if (condition.type == 1)
			{
				unsigned long tmp;
				condition.strValue.ToULong(&tmp);
				condition.value = tmp;
			}

			filter.filters.push_back(condition);

			pCondition = pCondition->NextSiblingElement("Condition");
		}

		if (!filter.filters.empty())
			m_globalFilters.push_back(filter);

		pFilter = pFilter->NextSiblingElement("Filter");
	}
	m_filters = m_globalFilters;

	TiXmlElement* pSets = pDocument->FirstChildElement("Sets");
	if (!pSets)
	{
		delete pDocument->GetDocument();
		return;
	}

	TiXmlElement* pSet = pSets->FirstChildElement("Set");
	while (pSet)
	{
		CFilterSet set;
		TiXmlElement* pItem = pSet->FirstChildElement("Item");
		while (pItem)
		{
			wxString local = GetTextElement(pItem, "Local");
			wxString remote = GetTextElement(pItem, "Remote");
			set.local.push_back(local == _T("1") ? true : false);
			set.remote.push_back(remote == _T("1") ? true : false);

			pItem = pItem->NextSiblingElement("Item");
		}
		if (set.local.size() == m_filters.size())
			m_globalFilterSets.push_back(set);

		pSet = pSet->NextSiblingElement("Set");
	}
	m_filterSets = m_globalFilterSets;

	delete pDocument->GetDocument();
}

void CFilterDialog::DisplayFilters()
{
	wxCheckListBox* pLocalFilters = XRCCTRL(*this, "ID_LOCALFILTERS", wxCheckListBox);
	wxCheckListBox* pRemoteFilters = XRCCTRL(*this, "ID_REMOTEFILTERS", wxCheckListBox);

	pLocalFilters->Clear();
	pRemoteFilters->Clear();

	for (unsigned int i = 0; i < m_filters.size(); i++)
	{
		const CFilter& filter = m_filters[i];
		pLocalFilters->Append(filter.name);
		pRemoteFilters->Append(filter.name);

		pLocalFilters->Check(i, m_filterSets[0].local[i]);
		pRemoteFilters->Check(i, m_filterSets[0].remote[i]);
	}
}

void CFilterDialog::OnMouseEvent(wxMouseEvent& event)
{
	m_shiftClick = event.ShiftDown();
	event.Skip();
}

void CFilterDialog::OnKeyEvent(wxKeyEvent& event)
{
	m_shiftClick = event.ShiftDown();
	event.Skip();
}

void CFilterDialog::OnFilterSelect(wxCommandEvent& event)
{
	wxCheckListBox* pLocal = XRCCTRL(*this, "ID_LOCALFILTERS", wxCheckListBox);
	wxCheckListBox* pRemote = XRCCTRL(*this, "ID_REMOTEFILTERS", wxCheckListBox);

	int item = event.GetSelection();

	if (m_shiftClick)
	{
		if (event.GetEventObject() == pLocal)
			pRemote->Check(item, pLocal->IsChecked(event.GetSelection()));
		else
			pLocal->Check(item, pRemote->IsChecked(event.GetSelection()));
	}

	bool localChecked = pLocal->IsChecked(event.GetSelection());
	bool remoteChecked = pRemote->IsChecked(event.GetSelection());
	m_filterSets[0].local[item] = localChecked;
	m_filterSets[0].remote[item] = remoteChecked;
}

bool CFilterDialog::FilenameFiltered(const wxString& name, bool dir, wxLongLong size, bool local) const
{
	// Check active filters
	for (unsigned int i = 0; i < m_filters.size(); i++)
	{
		if (local)
		{
			if (m_filterSets[0].local[i])
				if (FilenameFilteredByFilter(name, dir, size, i))
					return true;
		}
		else
		{
			if (m_filterSets[0].remote[i])
				if (FilenameFilteredByFilter(name, dir, size, i))
					return true;
		}
	}

	return false;
}

bool CFilterDialog::FilenameFilteredByFilter(const wxString& name, bool dir, wxLongLong size, unsigned int filterIndex) const
{
	wxRegEx regex;
	const CFilter& filter = m_globalFilters[filterIndex];

	if (dir && !filter.filterDirs)
		return false;
	else if (!filter.filterFiles)
		return false;
    
	for (std::vector<CFilterCondition>::const_iterator iter = filter.filters.begin(); iter != filter.filters.end(); iter++)
	{
		bool match = false;
		const CFilterCondition& condition = *iter;

		switch (condition.type)
		{
		case 0:
			switch (condition.condition)
			{
			case 0:
				if (filter.matchCase)
				{
					if (name.Contains(condition.strValue))
						match = true;
				}
				else
				{
					if (name.Lower().Contains(condition.strValue.Lower()))
						match = true;
				}
				break;
			case 1:
				if (filter.matchCase)
				{
					if (name == condition.strValue)
						match = true;
				}
				else
				{
					if (!name.CmpNoCase(condition.strValue))
						match = true;
				}
				break;
			case 2:
				wxASSERT(condition.pRegEx);
				if (condition.pRegEx->Matches(name))
					match = true;
			}
			break;
		case 1:
			if (size == -1)
				continue;
			switch (condition.condition)
			{
			case 0:
				if (size > condition.value)
					match = true;
				break;
			case 1:
				if (size == condition.value)
					match = true;
				break;
			case 2:
				if (size < condition.value)
					match = true;
				break;
			}
			break;
		}
		if (match && !filter.matchAll)
			return true;
	}
	
	return false;
}

bool CFilterDialog::CompileRegexes()
{
	for (unsigned int i = 0; i < m_globalFilters.size(); i++)
	{
		CFilter& filter = m_globalFilters[i];
		for (std::vector<CFilterCondition>::iterator iter = filter.filters.begin(); iter != filter.filters.end(); iter++)
		{
			CFilterCondition& condition = *iter;
			delete condition.pRegEx;
			if (!condition.type && condition.condition == 2)
				condition.pRegEx = new wxRegEx(condition.strValue);
			else
				condition.pRegEx = 0;
		}
	}
	return true;
}
