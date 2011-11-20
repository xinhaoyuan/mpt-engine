#include <iostream>
#include <string>
#include <map>
#include <set>
#include <fstream>
#include <deque>
#include <cstring>
#include <sstream>

using namespace std;

#include "script.hpp"

typedef long long handle_id;

ScriptEngine script;
map<handle_id, map<string, string> > data;
map<string, object_t> filters;
vector<handle_id> del, mod, ins;

struct View
{
	View               *mParent;
	string             *mName;
	map<string, View *> mSubviews;
	set<handle_id>      mItems;
	deque<handle_id>    mChangeList;
	string              mFilterName;
	object_t            mFilter;
};

struct Handle
{ handle_id id; };

static void
handleFree(object_t object)
{
	delete (Handle*)object->external.priv;
}

static struct see_external_type_s music_handle_type =
{ "Music Handle", external_type_dummy.enumerate, handleFree };
	
static object_t
handleNew(handle_id id)
{
	object_t result = script.ObjectNew();
	result->external.priv = new Handle;
	((Handle *)(result->external.priv))->id = id;
	result->external.type = &music_handle_type;
	OBJECT_TYPE_INIT(result, OBJECT_TYPE_EXTERNAL);
	return result;
}

void
updateView(View *view)
{		

	object_t lh = OBJECT_NULL;
	deque<handle_id>::iterator it;

	if (view->mFilter == OBJECT_NULL)
	{
		// Direct add all changes to the view
		for (it = view->mChangeList.begin(); it != view->mChangeList.end(); ++ it)
		{
			view->mItems.insert(*it);
		}

		return;
	}

	// Generate the list
	for (it = view->mChangeList.begin(); it != view->mChangeList.end(); ++ it)
	{
		object_t n = script.ObjectNew();
		object_t h = handleNew(*it);
		SLOT_INIT(n->pair.slot_car, h);
		SLOT_INIT(n->pair.slot_cdr, lh);
		OBJECT_TYPE_INIT(n, OBJECT_TYPE_PAIR);

		script.ObjectUnprotect(h);
		script.ObjectUnprotect(lh);
		lh = n;
	}

	string defaultSubFilterName = "";
	object_t defaultSubFilter = OBJECT_NULL;

	// Execute the filter
	vector<object_t> args, excall;
	args.push_back(lh);
	int r;
	object_t result;

	script.Apply(view->mFilter, &args);
	while (true)
	{
		r = script.Execute(result, &excall);
		// Process external call here
		if (r == APPLY_EXTERNAL_CALL)
		{
			if (xstring_equal_cstr(excall[0]->string, "MetaGet", -1))
			{
				handle_id id = ((Handle *)(excall[1]->external.priv))->id;
				string name(xstring_cstr(excall[2]->string), xstring_len(excall[2]->string));
				
				// cout << "meta get " << id << ' ' << name << endl;

				if (data.find(id) == data.end())
					result = OBJECT_NULL;
				else if (data[id].find(name) == data[id].end())
					result = OBJECT_NULL;
				else
				{
					string &value = data[id][name];
					result = script.ObjectNew();
					result->string = xstring_from_cstr(value.c_str(), value.length());
					OBJECT_TYPE_INIT(result, OBJECT_TYPE_STRING);
				}
			}
			else if (xstring_equal_cstr(excall[0]->string, "SubviewDefaultFilterSet", -1))
			{
				if (OBJECT_TYPE(excall[1]) == OBJECT_TYPE_STRING)
				{
					defaultSubFilterName = string(xstring_cstr(excall[1]->string));
					defaultSubFilter = filters[xstring_cstr(excall[1]->string)];
				}

				// cout << "default sub filter " << defaultSubFilterName << endl;
				
				result = OBJECT_NULL;
			}
			else if (xstring_equal_cstr(excall[0]->string, "SubviewPut", -1))
			{
				handle_id id = ((Handle *)(excall[1]->external.priv))->id;
				if (excall[2] == OBJECT_NULL)
				{
					view->mItems.insert(id);
				}
				else if (OBJECT_TYPE(excall[2]) == OBJECT_TYPE_STRING)
				{
					string name(xstring_cstr(excall[2]->string), xstring_len(excall[2]->string));
					
					// cout << "subview put " << id << ' ' << name << endl;
					
					if (view->mSubviews.find(name) == view->mSubviews.end())
					{
						View *sub = new View;
						sub->mName = new string(name);
						sub->mParent = view;
						sub->mFilter = OBJECT_NULL;
						
						view->mSubviews[name] = sub;
					}
					
					view->mSubviews[name]->mChangeList.push_back(id);
				}
			}
			else
			{
				cout << "ex : " << xstring_cstr(excall[0]->string) << endl;
				result = OBJECT_NULL;
			}
		}
		else break;

		for (int i = 0; i != excall.size(); ++ i)
		{
			script.ObjectUnprotect(excall[i]);
		}
	}

	// Recursive scan
	map<string, View *>::iterator sv;
	for (sv = view->mSubviews.begin(); sv != view->mSubviews.end(); ++ sv)
	{
		View *v = sv->second;
		if (v->mChangeList.empty()) continue;
		if (v->mFilter == OBJECT_NULL)
		{
			v->mFilter = defaultSubFilter;
			v->mFilterName = defaultSubFilterName;
		}
		updateView(v);
	}
}

void
writeAndFreeView(ostream &o, View *view)
{
	if (view->mParent != NULL)
	{
		o << view->mFilterName << ':' << *(view->mName) << '{' << endl;
	}
	else
	{
		o << view->mFilterName << endl;
	}

	// Items
	set<handle_id>::iterator item;
	for (item = view->mItems.begin(); item != view->mItems.end(); ++ item)
	{
		o << *item << endl;
	}
	
	// Recursive write
	map<string, View *>::iterator sv;
	for (sv = view->mSubviews.begin(); sv != view->mSubviews.end(); ++ sv)
	{
		View *v = sv->second;
		writeAndFreeView(o, v);
	}

	if (view->mParent != NULL)
	{
		o <<  '}' << endl;
		delete view->mName;
		delete view;
	}
}

int
GetKV(istream &in, string &k, string &v)
{
	ostringstream vbuf;
	if ((in >> k).fail()) return -1;

	vbuf.str("");
	char c;
	while (!(in.get(c)).fail()) if (c == '"') break;
	while (!(in.get(c)).fail())
	{
		if (c == '\\')
		{
			vbuf << c;
			in >> c;
		} else if (c == '"') break;
		vbuf << c;
	}
	v = vbuf.str();

	return 0;
}

void
update(bool fixed, string pl)
{
	// Generate the root change list
	ifstream playlist(pl.c_str());
	string line;
	View root;
	View *cur = &root;
	map<handle_id, View *> idMap;

	std::getline(playlist, root.mFilterName);
	
	root.mParent = NULL;
	if (filters.find(root.mFilterName) != filters.end())
		root.mFilter = filters[root.mFilterName];
	else root.mFilter = OBJECT_NULL;
	
	while (!std::getline(playlist, line).eof())
	{
		if (line.length() == 0) continue;
		
		if (line == "}")
		{
			if (cur->mParent == NULL) break;
			else cur = cur->mParent;
		}
		else if (line.at(line.length() - 1) == '{')
		{
			const char *cstr = line.c_str();
			const char *brk  = strchr(cstr, ':');

			string filter;
			string name;

			if (brk == NULL)
			{
				filter = "";
				name = string(cstr, line.length() - 1);
			}
			else
			{
				filter = string(cstr, brk - cstr);
				name = string(brk + 1, line.length() - (brk - cstr) - 2);
			}

			// cout << filter << " + " << name << endl;
			
			View *next;
			if (cur->mSubviews.find(name) == cur->mSubviews.end())
			{
				next = new View;
				next->mName = new string(name);
				next->mParent = cur;
				if (filters.find(filter) == filters.end())
					next->mFilter = OBJECT_NULL;
				else next->mFilter = filters[filter];
				next->mFilterName = filter;
				cur->mSubviews[name] = next;
			}
			else next = cur->mSubviews[name];
			cur = next;
		}
		else
		{
			handle_id id = atoi(line.c_str());
			cur->mItems.insert(id);
			idMap[id] = cur;
		}
	}

	playlist.close();

	vector<handle_id>::iterator it;
	for (it = del.begin(); it != del.end(); ++ it)
	{
		handle_id id = *it;
		if (idMap.find(id) != idMap.end())
		{
			cur = idMap[id];
			cur->mItems.erase(id);

			while (cur != &root)
			{
				if (cur->mItems.empty() && cur->mSubviews.empty())
				{
					string *name = cur->mName;
					cur = cur->mParent;
					delete cur->mSubviews[*name];
					cur->mSubviews.erase(*name);
					delete name;
				}
				else break;
			}
				
		}
	}


	for (it = mod.begin(); it != mod.end(); ++ it)
	{
		handle_id id = *it;
		if (idMap.find(id) != idMap.end())
		{
			cur = idMap[id];
			cur->mItems.erase(id);

			while (cur != &root)
			{
				if (cur->mItems.empty() && cur->mSubviews.empty())
				{
					string *name = cur->mName;
					cur = cur->mParent;
					delete cur->mSubviews[*name];
					cur->mSubviews.erase(*name);
					delete name;
				}
				else break;
			}
			
			root.mChangeList.push_back(id);
		}
	}

	for (it = ins.begin(); it != ins.end(); ++ it)
	{
		handle_id id = *it;
		if (idMap.find(id) != idMap.end())
		{
			cur = idMap[id];
			cur->mItems.erase(id);

			while (cur != &root)
			{
				if (cur->mItems.empty() && cur->mSubviews.empty())
				{
					string *name = cur->mName;
					cur = cur->mParent;
					delete cur->mSubviews[*name];
					cur->mSubviews.erase(*name);
					delete name;
				}
				else break;
			}
			
			root.mChangeList.push_back(id);
		}
		else if (!fixed)
		{
			root.mChangeList.push_back(id);
		}
	}


	updateView(&root);
	
	ofstream o(pl.c_str());
	writeAndFreeView(o, &root);
}

int
main(int argc, const char *argv[])
{
	vector<object_t> args, excall;
	const char *filter_fn = "filters.ss";
	
	// Read the filter script
	object_t result, file = script.LoadScript(filter_fn);
	script.Apply(file, &args);
	int r;
	
	while (true)
	{
		r = script.Execute(result, &excall);
		// Process external call here
		if (r == APPLY_EXTERNAL_CALL)
		{
			if (xstring_equal_cstr(excall[0]->string, "FilterExport", -1))
			{
				filters[xstring_cstr(excall[1]->string)] = excall[2];
				script.ObjectProtect(excall[2]);
				result = OBJECT_NULL;
			}
			else result = OBJECT_NULL;
		}
		else break;

		for (int i = 0; i != excall.size(); ++ i)
		{
			script.ObjectUnprotect(excall[i]);
		}
	}

	string line;
	ifstream cs(argv[1]);
	// Read the change set
	while (!std::getline(cs, line).eof())
	{
		if (line.length() == 0) continue;

		char op;
		handle_id id;
		istringstream ls(line);
		ls >> op >> id;

		if (op == '+')
		{
			ins.push_back(id);
			string key, value;
			while (GetKV(ls, key, value) == 0)
			{
				data[id][key] = value;
			}
		}
		else if (op == '*')
		{
			mod.push_back(id);
			string key, value;
			while (GetKV(ls, key, value) == 0)
			{
				data[id][key] = value;
			}
		}
		else if (op == '-')
		{
			del.push_back(id);
		}
	}
	cs.close();
	
	// Read list of playlist
	int i;
	for (i = 2; i < argc; ++ i)
	{
		string playlist(&argv[i][1]);
		update(argv[i][0] != '+', playlist);
	}
}	
