#ifndef FILEZILLA_INTERFACE_STATUSLINECTRL_HEADER
#define FILEZILLA_INTERFACE_STATUSLINECTRL_HEADER

class COptionsBase;
class CQueueView;
class CStatusLineCtrl final : public wxWindow
{
public:
	CStatusLineCtrl(CQueueView* pParent, COptionsBase & options, t_EngineData const* const pEngineData, wxRect const& initialPosition);
	~CStatusLineCtrl();

	const CFileItem* GetItem() const { return m_pEngineData->pItem; }

	void SetEngineData(const t_EngineData* const pEngineData);

	void SetTransferStatus(CTransferStatus const& status);
	void ClearTransferStatus();

	int64_t GetLastOffset() const { return status_.empty() ? m_lastOffset : status_.currentOffset; }
	int64_t GetTotalSize() const { return status_.empty() ? -1 : status_.totalSize; }
	wxFileOffset GetAverageSpeed(int elapsed_milli_seconds);
	wxFileOffset GetMomentarySpeed();

	virtual bool Show(bool show = true);

protected:
	void InitFieldOffsets();

	void DrawRightAlignedText(wxDC& dc, wxString const& text, int x, int y);
	void DrawProgressBar(wxDC& dc, int x, int y, int height, int bar_split, int permill);

	CQueueView* m_pParent;
	COptionsBase & options_;
	const t_EngineData* m_pEngineData;
	CTransferStatus status_;

	wxString m_statusText;
	wxTimer m_transferStatusTimer;

	static int m_fieldOffsets[4];
	static int m_barWidth;
	static wxCoord m_textHeight;
	static bool m_initialized;

	bool m_madeProgress;

	int64_t m_lastOffset{-1}; // Stores the last transfer offset so that the total queue size can be accurately calculated.

	// This is used by GetSpeed to forget about the first 10 seconds on longer transfers
	// since at the very start the speed is hardly accurate (e.g. due to TCP slow start)
	struct _past_data final
	{
		int elapsed{};
		wxFileOffset offset{};
	} m_past_data[10];
	int m_past_data_count{};

	//Used by GetMomentarySpeed
	struct monentary_speed_data {
		fz::monotonic_clock last_update;
		wxFileOffset last_offset{-1};
		wxFileOffset last_speed{-1};
	} m_monentary_speed_data;

	//Used to avoid excessive redraws
	wxBitmap m_data;
	std::unique_ptr<wxMemoryDC> m_mdc;
	wxString m_previousStatusText;
	int m_last_elapsed_seconds{};
	int m_last_left{};
	wxString m_last_bytes_and_rate;
	int m_last_bar_split{-1};
	int m_last_permill{-1};

	DECLARE_EVENT_TABLE()
	void OnPaint(wxPaintEvent& event);
	void OnTimer(wxTimerEvent& event);
	void OnEraseBackground(wxEraseEvent& event);
};

#endif
