#pragma once
// The structure of the native laptop's document pages (docs/ui/laptop.md §4.6, §4.8): which record of the
// game's text file is a heading, which is a paragraph, and what number the paragraph carries. The legacy
// pages hold this in their drawing code (AIMPolicies.cc's AimPolicyTextLocations, AIMHistory.cc's
// AimHistoryTextLocations, IMP_Text_System.cc's PrintImpText); stating it here as data keeps the layout
// testable without the game's data files and the loaders thin.
//
// The text itself still comes from the game's own files; nothing here is content.

#include <vector>

namespace LaptopContent
{
	/** A paragraph of a document: the record it is read from and the number the page prints beside it
	 * ("" when the page prints none). */
	struct Para
	{
		int record;
		char const* number;
	};

	/** A section of a document: the record of its heading and its numbered paragraphs, in order. */
	struct Section
	{
		int heading;
		std::vector<Para> paras;
	};

	// ---- A.I.M. policies (binarydata/aimpol.edt) ---------------------------------------------------------------
	// AIMPolicies.cc draws page 0 (AIM_STATEMENT_OF_POLICY and its two paragraphs) and then the nine numbered
	// sections (DEFINITIONS ... POLICY_MEDICAL). Page 1 there is the table of contents, which lists these
	// sections; the native page builds it from the same table.
	inline constexpr int AimPolicyStatementRecord = 0;
	inline constexpr int AimPolicySectionCount = 9;
	std::vector<int> const& AimPolicyStatementParas();
	std::vector<Section> const& AimPolicySections();

	// ---- A.I.M. history (binarydata/aimhist.edt) ---------------------------------------------------------------
	// AIMHistory.cc draws the five chapters starting at IN_THE_BEGINNING (record 6); the words of Colonel
	// Mohanned and the Dunn and Bradroad report are the closing paragraphs of the last two chapters.
	inline constexpr int AimHistoryChapterCount = 5;
	std::vector<Section> const& AimHistoryChapters();

	// ---- I.M.P. About Us (binarydata/imptext.edt) --------------------------------------------------------------
	// IMP_Text_System.cc lays the About Us page out in two columns; each heading is followed by its text.
	inline constexpr int ImpAboutSectionCount = 5;
	std::vector<Section> const& ImpAboutSections();

	// ---- A.I.M. alumni (laptop/old_aim.sti + binarydata/alumname.edt + binarydata/alumni.edt) -----------------
	// AIMArchives.cc pairs the three by one index: it blits frame `face_idx` of old_aim.sti, captions it with
	// row `face_idx` of alumname.edt and describes it with row `face_idx` of alumni.edt (face_idx = page * 20 +
	// position). The pairing is derived from that one number, never from a name lookup.
	inline constexpr int AimAlumniCount = 51;
	inline constexpr int AimAlumniFace(int index) { return index; }    // the old_aim.sti frame
	inline constexpr int AimAlumniNameRow(int index) { return index; } // the alumname.edt row (the caption)
	inline constexpr int AimAlumniBioRow(int index) { return index; }  // the alumni.edt row (name and bio)
	char const* AimAlumniFaceFile();                                   // the .sti the faces come from
} // namespace LaptopContent
