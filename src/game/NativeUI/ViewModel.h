#pragma once
// View models (docs/plan/native-modern-game.md, "View models decouple UI from game logic"): plain C++ that reads game
// state into fields and exposes commands that call the existing game functions. A view model declares its fields
// once (Describe); from that declaration
//   - Binding makes an RmlUi data model of it (RML uses {{field}}, data-for, data-event-click="command(args)"),
//   - Snapshot turns it into a tree for tests and automation (ja2.viewModel(name)).
// Change notification: game code calls Notify(TOPIC_...) where it sets its own "dirty" flags; view models that
// subscribed to the topic are refreshed before the next frame (UpdateAll) and their bindings redrawn.

#include "NativeUI.h"

#include <RmlUi/Core.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace NativeUI
{
	/** A tree of values (a view model's fields) for tests and automation. */
	struct Value
	{
		enum class Kind { String, Number, Bool, List, Object } kind = Kind::String;
		std::string str;
		double      num = 0;
		bool        flag = false;
		std::vector<Value> list;
		std::vector<std::pair<std::string, Value>> obj;

		static Value Str(std::string s) { Value v; v.str = std::move(s); return v; }
		static Value Num(double n) { Value v; v.kind = Kind::Number; v.num = n; return v; }
		static Value Bool(bool b) { Value v; v.kind = Kind::Bool; v.flag = b; return v; }
		Value const* Get(std::string const& key) const;
		std::string ToJson() const;
	};

	/** Fields of a row type (one element of a list field), by member pointer:
	 *   struct Person { std::string name; int age; static void Describe(RowFields<Person>& f) { f("name", &Person::name)("age", &Person::age); } }; */
	template<typename Row>
	struct RowFields
	{
		using Member = std::variant<std::string Row::*, int Row::*, bool Row::*, double Row::*>;
		std::vector<std::pair<std::string, Member>> members;
		template<typename T> RowFields& operator()(char const* name, T Row::* m) { members.emplace_back(name, Member(m)); return *this; }
	};

	class Fields;

	/** Type-erased list of rows (see Fields::Rows). */
	struct RowList
	{
		virtual ~RowList() = default;
		virtual void Bind(Rml::DataModelConstructor&, std::string const& name) = 0;
		virtual Value Snapshot() const = 0;
	};

	template<typename Row>
	struct RowListOf final : RowList
	{
		std::vector<Row>& rows;
		explicit RowListOf(std::vector<Row>& r) : rows(r) {}
		void Bind(Rml::DataModelConstructor& c, std::string const& name) override
		{
			RowFields<Row> f;
			Row::Describe(f);
			if (auto s = c.RegisterStruct<Row>())
			{
				for (auto const& [n, m] : f.members)
					std::visit([&](auto ptr) { s.RegisterMember(n, ptr); }, m);
			}
			c.RegisterArray<std::vector<Row>>();
			c.Bind(name, &rows);
		}
		Value Snapshot() const override
		{
			RowFields<Row> f;
			Row::Describe(f);
			Value list;
			list.kind = Value::Kind::List;
			for (Row const& r : rows)
			{
				Value o;
				o.kind = Value::Kind::Object;
				for (auto const& [n, m] : f.members)
				{
					std::visit([&](auto ptr) {
						using T = std::decay_t<decltype(r.*ptr)>;
						if constexpr (std::is_same_v<T, std::string>) o.obj.emplace_back(n, Value::Str(r.*ptr));
						else if constexpr (std::is_same_v<T, bool>) o.obj.emplace_back(n, Value::Bool(r.*ptr));
						else o.obj.emplace_back(n, Value::Num(double(r.*ptr)));
					}, m);
				}
				list.list.push_back(std::move(o));
			}
			return list;
		}
	};

	/** What a view model's Describe is given. */
	class Fields
	{
	public:
		virtual ~Fields() = default;
		virtual void Field(char const* name, std::string&) = 0;
		virtual void Field(char const* name, int&) = 0;
		virtual void Field(char const* name, bool&) = 0;
		virtual void Field(char const* name, double&) = 0;
		virtual void List(char const* name, std::vector<std::string>&) = 0;
		template<typename Row> void Rows(char const* name, std::vector<Row>& rows) { AddRows(name, std::make_unique<RowListOf<Row>>(rows)); }
	protected:
		virtual void AddRows(char const* name, std::unique_ptr<RowList>) = 0;
	};

	using Args = std::vector<std::string>;

	class ViewModel
	{
	public:
		ViewModel(std::string name, uint32_t topics);
		virtual ~ViewModel();
		ViewModel(ViewModel const&) = delete;
		ViewModel& operator=(ViewModel const&) = delete;

		std::string const& Name() const { return m_name; }
		uint32_t Topics() const { return m_topics; }

		/** Reads the game state into the fields. */
		virtual void Refresh() {}
		/** Declares the fields (and nothing else). */
		virtual void Describe(Fields&) = 0;

		/** Commands: what the UI (and tests) can ask the view model to do. */
		void Command(std::string const& name, std::function<void(Args const&)> fn);
		bool Invoke(std::string const& name, Args const& args = {});
		std::vector<std::string> CommandNames() const;

		/** Refreshes now if a subscribed topic was notified (or always with @a force); tells the binding. */
		void Update(bool force = false);
		/** The fields changed without a Refresh (a command changed them): redraw the binding. */
		void Changed();
		bool Stale() const { return m_stale; }
		uint64_t Revision() const { return m_revision; }

		Value Snapshot();

		/** Set by Binding. */
		std::function<void()> onChanged;

		/** Every live view model (for Notify and automation). */
		static std::vector<ViewModel*> const& Live();
		static ViewModel* Find(std::string const& name);
		/** Refreshes the stale ones (once per frame). */
		static void UpdateAll();

	private:
		friend void Notify(uint32_t);
		std::string m_name;
		uint32_t    m_topics;
		bool        m_stale = true;
		uint64_t    m_revision = 0;
		std::map<std::string, std::function<void(Args const&)>> m_commands;
	};

	/** An RmlUi data model named after the view model: fields bound by reference, commands as event callbacks
	 * (RML: data-event-click="back" or data-event-click="select(3)"). Removed with the binding. */
	class Binding
	{
	public:
		Binding(Rml::Context*, ViewModel&);
		~Binding();
		Rml::DataModelHandle Handle() const { return m_handle; }
	private:
		Rml::Context*        m_ctx;
		ViewModel&           m_vm;
		Rml::DataModelHandle m_handle;
		std::vector<std::unique_ptr<RowList>> m_rows;
	};

	/** View models automation can create on demand by name (ja2.viewModel("status")). */
	void RegisterViewModelFactory(std::string const& name, std::function<std::unique_ptr<ViewModel>()>);
	std::unique_ptr<ViewModel> MakeViewModel(std::string const& name);
	std::vector<std::string> ViewModelFactoryNames();
}
