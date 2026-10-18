// The document layout tables of the native laptop (LaptopContent.h). Every record number is the one the
// legacy page reads: AIMPolicies.cc / AIMHistory.cc / IMP_Text_System.cc and the .edt files in
// assets/. These are structural facts of the original data, so the loader and the layout can be tested
// separately.
#include "LaptopContent.h"

#include "Directories.h"

namespace LaptopContent
{

std::vector<int> const& AimPolicyStatementParas()
{
	// AIM_STATEMENT_OF_POLICY_1 and _2
	static std::vector<int> const v = { 1, 2 };
	return v;
}

std::vector<Section> const& AimPolicySections()
{
	// AIMPolicies.cc's eleven pages, pages 2..10: DEFINITIONS through POLICY_MEDICAL. Each entry is
	// (heading record, [(number, record) ...]); the numbers are what DisplayAimPolicyParagraph and
	// DisplayAimPolicySubParagraph print beside them.
	static std::vector<Section> const v = {
		/* DEFINITIONS */ { 3, { { 4, "1.1" }, { 5, "1.2" }, { 6, "1.3" }, { 7, "1.4" } } },
		/* LENGTH_OF_ENGAGEMENT */
		{ 8, { { 9, "2.1" }, { 10, "2.11" }, { 11, "2.12" }, { 12, "2.13" }, { 13, "2.2" } } },
		/* LOCATION_OF_ENGAGEMENT */
		{ 14,
		  { { 15, "3.1" },
			{ 16, "3.2" },
			{ 17, "3.21" },
			{ 18, "3.22" },
			{ 19, "3.23" },
			{ 20, "3.3" },
			{ 21, "3.4" } } },
		/* CONTRACT_EXTENSIONS */ { 22, { { 23, "4.1" }, { 24, "4.2" }, { 25, "4.3" } } },
		/* TERMS_OF_PAYMENT */ { 26, { { 27, "5.1" } } },
		/* TERMS_OF_ENGAGEMENT */ { 28, { { 29, "6.1" }, { 30, "6.2" }, { 31, "" } } },
		/* ENGAGEMENT_TERMINATION */ { 32, { { 33, "7.1" }, { 34, "7.11" }, { 35, "7.12" }, { 36, "7.13" } } },
		/* EQUIPMENT_AND_INVENTORY */ { 37, { { 38, "8.1" }, { 39, "8.2" } } },
		/* POLICY_MEDICAL */ { 40, { { 41, "9.1" }, { 42, "9.2" }, { 43, "9.3" }, { 44, "" }, { 45, "9.4" } } },
	};
	return v;
}

std::vector<Section> const& AimHistoryChapters()
{
	// AIMHistory.cc's five pages, 1..5 (AimHistoryTextLocations, records 6..22). The last two chapters end
	// with the words of Colonel Mohanned and the Dunn and Bradroad report.
	static std::vector<Section> const v = {
		/* IN_THE_BEGINNING */ { 6, { { 7, "" }, { 8, "" } } },
		/* THE_ISLAND_METAVIRA */ { 9, { { 10, "" }, { 11, "" } } },
		/* GUS_TARBALLS */ { 12, { { 13, "" }, { 14, "" } } },
		/* WORD_FROM_FOUNDER */ { 15, { { 16, "" }, { 17, "" } } },
		/* INCORPORATION */ { 18, { { 19, "" }, { 20, "" }, { 21, "" }, { 22, "" } } },
	};
	return v;
}

std::vector<Section> const& ImpAboutSections()
{
	// IMP_Text_System.cc's IMP_ABOUT_US case, left column then right: "Can You Be Your Own Worst Enemy?"
	// (its second text, record 12, is empty in the shipped data so it reads as one paragraph), "Why you need
	// us:", "What you get:", "How much:", "When:".
	static std::vector<Section> const v = {
		{ 10, { { 11, "" }, { 12, "" } } },
		{ 19, { { 13, "" } } },
		{ 20, { { 14, "" } } },
		{ 17, { { 15, "" } } },
		{ 18, { { 16, "" } } },
	};
	return v;
}

char const* AimAlumniFaceFile()
{
	return LAPTOPDIR "/old_aim.sti";
}

} // namespace LaptopContent
