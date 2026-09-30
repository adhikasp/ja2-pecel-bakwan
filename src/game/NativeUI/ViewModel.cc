#include "ViewModel.h"
#include "NativeUIRuntime.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace NativeUI
{

namespace
{
	std::vector<ViewModel*>& LiveList()
	{
		static std::vector<ViewModel*> live;
		return live;
	}

	std::map<std::string, std::function<std::unique_ptr<ViewModel>()>>& Factories()
	{
		static std::map<std::string, std::function<std::unique_ptr<ViewModel>()>> f;
		return f;
	}

	void JsonString(std::ostringstream& o, std::string const& s)
	{
		o << '"';
		for (char const c : s)
		{
			switch (c)
			{
				case '"':  o << "\\\""; break;
				case '\\': o << "\\\\"; break;
				case '\n': o << "\\n"; break;
				default:   o << c;
			}
		}
		o << '"';
	}

	void ToJson(std::ostringstream& o, Value const& v)
	{
		switch (v.kind)
		{
			case Value::Kind::String: JsonString(o, v.str); break;
			case Value::Kind::Bool:   o << (v.flag ? "true" : "false"); break;
			case Value::Kind::Number:
			{
				char buf[32];
				std::snprintf(buf, sizeof buf, "%g", v.num);
				o << buf;
				break;
			}
			case Value::Kind::List:
				o << '[';
				for (size_t i = 0; i < v.list.size(); ++i) { if (i) o << ','; ToJson(o, v.list[i]); }
				o << ']';
				break;
			case Value::Kind::Object:
				o << '{';
				for (size_t i = 0; i < v.obj.size(); ++i)
				{
					if (i) o << ',';
					JsonString(o, v.obj[i].first);
					o << ':';
					ToJson(o, v.obj[i].second);
				}
				o << '}';
				break;
		}
	}

	/** Describe -> Value */
	class SnapshotFields final : public Fields
	{
	public:
		Value out;
		SnapshotFields() { out.kind = Value::Kind::Object; }
		void Field(char const* n, std::string& v) override { out.obj.emplace_back(n, Value::Str(v)); }
		void Field(char const* n, int& v) override { out.obj.emplace_back(n, Value::Num(v)); }
		void Field(char const* n, bool& v) override { out.obj.emplace_back(n, Value::Bool(v)); }
		void Field(char const* n, double& v) override { out.obj.emplace_back(n, Value::Num(v)); }
		void List(char const* n, std::vector<std::string>& v) override
		{
			Value l;
			l.kind = Value::Kind::List;
			for (auto const& s : v) l.list.push_back(Value::Str(s));
			out.obj.emplace_back(n, std::move(l));
		}
	protected:
		void AddRows(char const* n, std::unique_ptr<RowList> rows) override { out.obj.emplace_back(n, rows->Snapshot()); }
	};

	/** Describe -> an RmlUi data model */
	class RmlFields final : public Fields
	{
	public:
		Rml::DataModelConstructor& c;
		std::vector<std::unique_ptr<RowList>>& keep;
		bool stringsRegistered = false;
		RmlFields(Rml::DataModelConstructor& ctor, std::vector<std::unique_ptr<RowList>>& k) : c(ctor), keep(k) {}
		void Field(char const* n, std::string& v) override { c.Bind(n, &v); }
		void Field(char const* n, int& v) override { c.Bind(n, &v); }
		void Field(char const* n, bool& v) override { c.Bind(n, &v); }
		void Field(char const* n, double& v) override { c.Bind(n, &v); }
		void List(char const* n, std::vector<std::string>& v) override
		{
			if (!stringsRegistered) { c.RegisterArray<std::vector<std::string>>(); stringsRegistered = true; }
			c.Bind(n, &v);
		}
	protected:
		void AddRows(char const* n, std::unique_ptr<RowList> rows) override
		{
			rows->Bind(c, n);
			keep.push_back(std::move(rows));
		}
	};
}

Value const* Value::Get(std::string const& key) const
{
	for (auto const& [k, v] : obj) if (k == key) return &v;
	return nullptr;
}

std::string Value::ToJson() const
{
	std::ostringstream o;
	NativeUI::ToJson(o, *this);
	return o.str();
}

void Notify(uint32_t const topics)
{
	for (ViewModel* vm : LiveList())
	{
		if (vm->m_topics & topics) vm->m_stale = true;
	}
}

ViewModel::ViewModel(std::string name, uint32_t const topics) : m_name(std::move(name)), m_topics(topics)
{
	LiveList().push_back(this);
}

ViewModel::~ViewModel()
{
	std::erase(LiveList(), this);
}

void ViewModel::Command(std::string const& name, std::function<void(Args const&)> fn)
{
	m_commands[name] = std::move(fn);
}

bool ViewModel::Invoke(std::string const& name, Args const& args)
{
	auto it = m_commands.find(name);
	if (it == m_commands.end()) return false;
	it->second(args);
	return true;
}

std::vector<std::string> ViewModel::CommandNames() const
{
	std::vector<std::string> out;
	for (auto const& [k, v] : m_commands) out.push_back(k);
	return out;
}

void ViewModel::Update(bool const force)
{
	if (!m_stale && !force) return;
	m_stale = false;
	Refresh();
	Changed();
}

void ViewModel::Changed()
{
	++m_revision;
	if (onChanged) onChanged();
}

Value ViewModel::Snapshot()
{
	SnapshotFields f;
	Describe(f);
	return f.out;
}

std::vector<ViewModel*> const& ViewModel::Live() { return LiveList(); }

ViewModel* ViewModel::Find(std::string const& name)
{
	for (ViewModel* vm : LiveList()) if (vm->Name() == name) return vm;
	return nullptr;
}

void ViewModel::UpdateAll()
{
	// a refresh may create or destroy view models: iterate over a copy
	std::vector<ViewModel*> const live = LiveList();
	for (ViewModel* vm : live)
	{
		if (std::find(LiveList().begin(), LiveList().end(), vm) != LiveList().end()) vm->Update();
	}
}

Binding::Binding(Rml::Context* ctx, ViewModel& vm) : m_ctx(ctx), m_vm(vm)
{
	Rml::DataModelConstructor c = ctx->CreateDataModel(vm.Name());
	if (!c) throw std::runtime_error("RmlUi: cannot create data model " + vm.Name());
	RmlFields f(c, m_rows);
	vm.Describe(f);
	for (std::string const& cmd : vm.CommandNames())
	{
		c.BindEventCallback(cmd, [this, cmd](Rml::DataModelHandle, Rml::Event&, Rml::VariantList const& a) {
			Args args;
			for (auto const& v : a) args.push_back(v.Get<Rml::String>());
			m_vm.Invoke(cmd, args);
		});
	}
	m_handle = c.GetModelHandle();
	vm.onChanged = [this] {
		m_handle.DirtyAllVariables();
		Invalidate();
	};
}

Binding::~Binding()
{
	m_vm.onChanged = nullptr;
	if (m_ctx) m_ctx->RemoveDataModel(m_vm.Name());
}

void RegisterViewModelFactory(std::string const& name, std::function<std::unique_ptr<ViewModel>()> make)
{
	Factories()[name] = std::move(make);
}

std::unique_ptr<ViewModel> MakeViewModel(std::string const& name)
{
	auto it = Factories().find(name);
	return it == Factories().end() ? nullptr : it->second();
}

std::vector<std::string> ViewModelFactoryNames()
{
	std::vector<std::string> out;
	for (auto const& [k, v] : Factories()) out.push_back(k);
	return out;
}

}
