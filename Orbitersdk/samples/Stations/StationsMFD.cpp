#define STRCIT
#define ORBITER_MODULE

#include <Windows.h>

#include <orbitersdk.h>
#include <Shlwapi.h>
#include <process.h> 
#include <string>
#include <iostream>
#include <sstream>
#include <vector>
#include <io.h>
#include <map>
#include "StationsMFD.h"

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <iostream>

#pragma comment(lib, "Ws2_32.lib")

using namespace rapidjson;

int mode;
bool init = false;

std::vector<std::map<OBJHANDLE, bool>> stations;
std::vector<int> station_ids;
std::vector<OBJHANDLE> station_vessels;

void curl(std::string cmd) {
	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));

	CreateProcess(NULL,   // No module name (use command line)
		(LPSTR)(std::string("Utils\\curl.exe ") + cmd).c_str(),        // Command line
		NULL,           // Process handle not inheritable
		NULL,           // Thread handle not inheritable
		FALSE,          // Set handle inheritance to FALSE
		CREATE_NO_WINDOW,              // No creation flags
		NULL,           // Use parent's environment block
		NULL,           // Use parent's starting directory 
		&si,            // Pointer to STARTUPINFO structure
		&pi);
}

void post_json(std::string url, std::string json) {
	FILE* f = fopen("Modules\\pj.json", "w");
	if (f == 0) {
		return;
	}
	fwrite(json.c_str(), 1, json.length(), f);
	fclose(f);
	
	curl(" -X POST -H \"Content-Type: application/json\" -d \"@Modules\\pj.json\" " + url);
}

int find_dock_index(OBJHANDLE target, OBJHANDLE haystack) {
	VESSEL* v = oapiGetVesselInterface(target);
	for (auto i = 0; i < v->DockCount(); i++) {
		auto dock = v->GetDockHandle(i);
		auto test = v->GetDockStatus(dock);
		if (haystack == test) return i;
	}
	return -1;
}

void build_dock_links(OBJHANDLE root_node, OBJHANDLE parent, Document &d) {
	if (!oapiIsVessel(root_node)) return;

	Document::AllocatorType& a = d.GetAllocator();

	// get the docked vessels
	VESSEL* v = oapiGetVesselInterface(root_node);
	auto dock_count = v->DockCount();

	char parent_name[256];
	char parent_classname[256];

	strcpy(parent_name, v->GetName());
	strcpy(parent_classname, v->GetClassNameA());

	char child_name[256];
	char child_classname[256];

	for (auto i = 0; i < dock_count; i++) {
		auto dock = v->GetDockHandle(i);
		auto test = v->GetDockStatus(dock);
		if (!oapiIsVessel(test)) continue;
		// ignore parent link
		if (test != parent) {
			// find child dock index
			auto child_dock_inx = find_dock_index(test, root_node);
			auto parent_dock_inx = i;
			VESSEL* childv = oapiGetVesselInterface(test);
			strcpy(child_name, childv->GetName());
			strcpy(child_classname, childv->GetClassNameA());
			Value v;
			v.SetObject();

			Value pn(parent_name, a);
			v.AddMember("parent_name", pn, a);

			Value pcn(parent_classname, a);
			v.AddMember("parent_classname", pcn, a);

			Value cn(child_name, a);
			v.AddMember("child_name", cn, a);

			Value ccn(child_classname, a);
			v.AddMember("child_classname", ccn, a);

			v.AddMember("parent_dock_index", i, a);
			v.AddMember("child_dock_index", child_dock_inx, a);

			d.PushBack(v, a);

			build_dock_links(test, root_node, d);
		}
		else {

		}
	}
}

std::string serialize(Document &d) {
	GenericStringBuffer<UTF8<>> sbuf;
	Writer<GenericStringBuffer<UTF8<>>> writer(sbuf);
	d.Accept(writer);
	std::string str = sbuf.GetString();
	return str;
}

std::string serialize_focus() {
	Document d;
	d.SetArray();
	build_dock_links(oapiGetFocusInterface()->GetHandle(), NULL, d);

	return serialize(d);
}

std::map<OBJHANDLE, bool> create_station(std::string station_str, VECTOR3 rvel, VECTOR3 rpos, OBJHANDLE ref) {
	Document d;
	d.Parse(station_str.c_str());

	VESSEL* v = oapiGetFocusInterface();
	VESSELSTATUS2 vs;
	memset(&vs, 0, sizeof(vs));
	vs.version = 2;
	vs.rbody = ref;
	vs.rpos = rpos;
	vs.rvel = rvel;


	std::map<OBJHANDLE, bool> vessel_lookup;

	for (int i = 0; i < d.Size(); i++) {
		auto parent_name = d[i]["parent_name"].GetString();
		auto parent_classname = d[i]["parent_classname"].GetString();
		auto child_name = d[i]["child_name"].GetString();
		auto child_classname = d[i]["child_classname"].GetString();

		auto parent_dock_index = d[i]["parent_dock_index"].GetInt();
		auto child_dock_index = d[i]["child_dock_index"].GetInt();

		OBJHANDLE parent = oapiGetVesselByName((char*)parent_name);
		OBJHANDLE child = oapiGetVesselByName((char*)child_name);

		if (!oapiIsVessel(parent)) {
			parent = oapiCreateVesselEx(parent_name, parent_classname, &vs);
		}

		if (!oapiIsVessel(child)) {
			child = oapiCreateVesselEx(child_name, child_classname, &vs);
		}

		vessel_lookup[parent] = true;
		vessel_lookup[child] = true;

		VESSEL* vp = oapiGetVesselInterface(parent);
		vp->Dock(child, parent_dock_index, child_dock_index, 2);
	}
	return vessel_lookup;
}

std::string prepare_post_station(VECTOR3 vrvel, VECTOR3 vrpos, std::string ref_body, std::string station_str, int station_id = -1) {
	Document d, body;
	d.Parse(station_str.c_str());

	Document::AllocatorType& a = body.GetAllocator();
	body.SetObject();

	Value station_links(station_str.c_str(), a);
	body.AddMember("station_links", station_links, a);

	Document orbit_def;
	Document::AllocatorType& da = orbit_def.GetAllocator();
	orbit_def.SetObject();

	char buffer[512];
	sprintf(buffer, "%.6f,%.6f,%.6f", vrvel.x, vrvel.y, vrvel.z);
	Value rvel(buffer, da);
	orbit_def.AddMember("rvel", rvel, da);

	sprintf(buffer, "%.6f,%.6f,%.6f", vrpos.x, vrpos.y, vrpos.z);
	Value rpos(buffer, da);
	orbit_def.AddMember("rpos", rpos, da);

	Value rbody(ref_body.c_str(), da);
	orbit_def.AddMember("rbody", rbody, da);

	Value od(serialize(orbit_def).c_str(), da);
	body.AddMember("orbit_def", od, a);

	if (station_id != -1) {
		Value sid;
		sid.SetInt(station_id);
		body.AddMember("station_id", sid, a);
	}

	auto post_body = serialize(body);
	return post_body;
}

DLLCLBK void opcPreStep(double simt, double simdt, double mjd)
{
	if (!init) {
		FILE* f = fopen("stations.json", "r");
		if (f != 0) {
			fclose(f);
			std::ifstream t("stations.json");
			std::stringstream buffer;
			buffer << t.rdbuf();
			std::string stations_str = buffer.str();

			Document d;
			d.Parse(stations_str.c_str());
			for (int i = 0; i < d.Size(); i++) {
				auto id = d[i]["id"].GetInt();
				auto station_links = d[i]["station_links"].GetString();
				auto orbit_def = d[i]["orbit_def"].GetString();

				Document od;
				od.Parse(orbit_def);
				VECTOR3 rpos, rvel;
				auto rv = od["rvel"].GetString();
				auto rp = od["rpos"].GetString();
				auto rbody = od["rbody"].GetString();
				sscanf(rv, "%lf,%lf,%lf", &rvel.x, &rvel.y, &rvel.z);
				sscanf(rp, "%lf,%lf,%lf", &rpos.x, &rpos.y, &rpos.z);

				auto station_nodes = create_station(station_links, rvel, rpos, oapiGetObjectByName((char*)rbody));
				stations.push_back(station_nodes);
				station_ids.push_back(id);
			}
		}
		init = true;
	}
}


DLLCLBK void InitModule(HINSTANCE hModule) {
	static char* name = "Simple MFD";
	MFDMODESPEC spec;
	spec.name = name;
	spec.key = OAPI_KEY_S;
	spec.msgproc = SimpleMFD::MsgProc;
	mode = oapiRegisterMFDMode(spec);

	curl("-o stations.json https://orbiter-mods.com/stations");
}

DLLCLBK void opcDLLExit (HINSTANCE hDLL)
{
	oapiUnregisterMFDMode (mode);
}

SimpleMFD::SimpleMFD(DWORD w, DWORD h, VESSEL *vessel)
: MFD (w, h, vessel)
{
	height=h/24;
	width=w/35;

	monitor_flag = false;
}
SimpleMFD::~SimpleMFD()
{
}
int SimpleMFD::MsgProc (UINT msg, UINT mfd, WPARAM wparam, LPARAM lparam)
{
	switch (msg) {
	case OAPI_MSG_MFD_OPENED:
		return (int)(new SimpleMFD (LOWORD(wparam), HIWORD(wparam), (VESSEL*)lparam));
	}
	return 0;
}

void SimpleMFD::Update (HDC hDC)
{
	Title (hDC, "Stations");
	char buffer[256];
	int ystep = this->height, y = ystep * 5;

	int len = sprintf(buffer, "Press Shift + A to share the focus station");
	TextOut(hDC, 10, y, buffer, len);
	y += ystep;
}

bool is_station(OBJHANDLE ref) {
	VESSEL* v = oapiGetVesselInterface(ref);
	auto dc = v->DockCount();
	if (dc < 1) {
		return false;
	}
	for (int i = 0; i < dc; i++) {
		auto dh = v->GetDockHandle(i);
		auto ds = v->GetDockStatus(dh);
		if (oapiIsVessel(ds)) {
			return true;
		}
	}
	return false;
}

bool can_create_station(OBJHANDLE ref) {
	std::vector<OBJHANDLE> check_list;
	check_list.push_back(ref);

	// find all vessels docked
	VESSEL* v = oapiGetVesselInterface(ref);
	auto dc = v->DockCount();
	for (int i = 0; i < dc; i++) {
		auto dh = v->GetDockHandle(i);
		auto ds = v->GetDockStatus(dh);
		if (oapiIsVessel(ds)) {
			check_list.push_back(ds);
		}
	}

	// check if any vessel is in the station_vessel list
	for (int i = 0; i < check_list.size(); i++) {
		auto t1 = check_list[i];
		for (int j = 0; j < station_vessels.size(); j++) {
			auto t2 = station_vessels[j];
			if (t1 == t2) {
				return false;
			}
		}
	}
	
	return true;
}

bool SimpleMFD::ConsumeKeyBuffered(DWORD key)
{
	switch (key) {
		case OAPI_KEY_A: {
			VESSEL* fv = oapiGetFocusInterface();
			OBJHANDLE focus = fv->GetHandle();
			if (!is_station(focus)) {
				sprintf(oapiDebugString(), "Focus is not a station, it has nothing docked to it");
				return false;
			}
			int station_id = -1;
			for (int i = 0; i < stations.size(); i++) {
				auto station_check = stations[i];
				if (station_check.find(focus) != station_check.end()) {
					station_id = station_ids[i];
					break;
				}
			}

			if (station_id == -1 && !can_create_station(focus)) {
				sprintf(oapiDebugString(), "This station was added in simulation session, restart to make changes");
				return false;
			}
			VECTOR3 pos, vel;
			char surf_name[256];
			auto ref = fv->GetSurfaceRef();
			oapiGetObjectName(ref, surf_name, 256);
			fv->GetRelativePos(ref, pos);
			fv->GetRelativeVel(ref, vel);
			auto focus_str = serialize_focus();
			auto post_body = prepare_post_station(vel, pos, surf_name, focus_str, station_id);
			post_json("https://orbiter-mods.com/station", post_body);
			if (station_id == -1) {
				auto st_check = create_station(focus_str, vel, pos, ref);
				std::map<OBJHANDLE, bool>::iterator it;
				for (it = st_check.begin(); it != st_check.end(); it++) {
					auto v = it->first;
					station_vessels.push_back(v);
				}
			}
			sprintf(oapiDebugString(), "Station saved into the world");
			return true;
		}
	}
	return false;
}

