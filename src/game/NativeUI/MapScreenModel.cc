#include "MapScreenModel.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace NativeUI::MapModel
{

namespace
{
	constexpr int ON_DUTY = 20, IN_TRANSIT = 24, ASSIGNMENT_DEAD = 30;

	std::string Trim(std::string s)
	{
		s.erase(0, s.find_first_not_of(' '));
		size_t const e = s.find_last_not_of(' ');
		s.erase(e == std::string::npos ? 0 : e + 1);
		return s;
	}
}

Group GroupOf(MercFact const& m)
{
	if (m.vehicle) return Group::Vehicles;
	if (m.dead || m.assignment == ASSIGNMENT_DEAD) return Group::Dead;
	if (m.assignment == IN_TRANSIT) return Group::Transit;
	if (m.assignment >= 0 && m.assignment < ON_DUTY) return Group::Squad;
	return Group::Other;
}

std::vector<TeamEntry> GroupTeam(std::vector<MercFact> const& mercs, bool const grouped)
{
	std::vector<TeamEntry> out;
	for (MercFact const& m : mercs)
	{
		TeamEntry e;
		e.fact = m;
		e.group = GroupOf(m);
		e.squad = e.group == Group::Squad ? m.assignment : -1;
		out.push_back(e);
	}
	if (!grouped) return out;
	std::stable_sort(out.begin(), out.end(), [](TeamEntry const& a, TeamEntry const& b) {
		if (a.group != b.group) return int(a.group) < int(b.group);
		return a.squad < b.squad;
	});
	for (size_t i = 0; i < out.size(); ++i)
	{
		out[i].firstInGroup = i == 0 || out[i].group != out[i - 1].group || out[i].squad != out[i - 1].squad;
	}
	return out;
}

ContractOption ParseContractLine(std::string const& line)
{
	ContractOption o;
	size_t const open = line.rfind('(');
	size_t const close = line.rfind(')');
	if (open == std::string::npos || close == std::string::npos || close < open)
	{
		o.label = Trim(line);
		return o;
	}
	o.label = Trim(line.substr(0, open));
	std::string digits;
	for (size_t i = open; i < close; ++i) if (std::isdigit(static_cast<unsigned char>(line[i]))) digits += line[i];
	o.price = digits.empty() ? -1 : std::atoi(digits.c_str());
	return o;
}

MoveLine ParseMoveLine(std::string const& line)
{
	MoveLine m;
	m.merc = line.rfind("   ", 0) == 0;
	std::string t = Trim(line);
	if (t.size() >= 2 && t.front() == '*' && t.back() == '*')
	{
		m.checked = true;
		t = t.substr(1, t.size() - 2);
	}
	m.label = Trim(t);
	return m;
}

MessageKind Classify(bool const red, bool const dialog, std::string const& text)
{
	if (red) return MessageKind::Combat;
	if (text.find('$') != std::string::npos) return MessageKind::Money;
	if (dialog) return MessageKind::Team;
	return MessageKind::Info;
}

std::string SectorNamed(std::string const& text)
{
	for (size_t i = 0; i + 1 < text.size(); ++i)
	{
		char const c = text[i];
		if (c < 'A' || c > 'P') continue;
		if (i > 0 && std::isalnum(static_cast<unsigned char>(text[i - 1]))) continue;
		size_t j = i + 1;
		while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j]))) ++j;
		if (j == i + 1 || j - i > 3) continue;
		if (j < text.size() && std::isalnum(static_cast<unsigned char>(text[j]))) continue;
		int const n = std::atoi(text.substr(i + 1, j - i - 1).c_str());
		if (n >= 1 && n <= 16) return text.substr(i, j - i);
	}
	return {};
}

// the game clock counts from midnight of day 0, so the campaign starts on day 1 (GetWorldDay)
int DayOf(uint32_t const minute) { return std::max(1, int(minute / (24 * 60))); }

std::string ClockOf(uint32_t const minute)
{
	char buf[8];
	std::snprintf(buf, sizeof(buf), "%02u:%02u", unsigned(minute / 60 % 24), unsigned(minute % 60));
	return buf;
}

bool Matches(std::string const& text, std::string const& query)
{
	if (query.empty()) return true;
	auto low = [](std::string s) { for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; };
	return low(text).find(low(query)) != std::string::npos;
}

int ItemPixelScale(float const dpScale)
{
	return std::clamp(int(std::lround(2.0 * dpScale)), 1, 4);
}

ItemFit FitItem(int const w, int const h, float const boxW, float const boxH, float const dpScale)
{
	ItemFit f;
	int const bw = int(boxW * dpScale), bh = int(boxH * dpScale);
	int k = ItemPixelScale(dpScale);
	while (k > 1 && (w * k > bw || h * k > bh)) --k;
	f.scale = k;
	f.w = w * k;
	f.h = h * k;
	f.left = (bw - f.w) / 2;
	f.top = (bh - f.h) / 2;
	return f;
}

}
