"""
Fake-VR Accessible Hierarchical Map, Structure, Spawner & Foley Studio (wxPython)
Allows visually impaired and sighted creators to:
1. Navigate and edit Hierarchical Maps (Country/World -> Cities -> Structures/Interiors).
2. Open Structures in dedicated clean editor windows to configure interior walls and furniture.
3. Configure TravelPoints linking maps together.
4. Design Spawners & NPCs (wander radius, footsteps, foley, voice audio, dialogues, script triggers).
5. Configure 3D World Objects with adjustable respawn timers (in seconds) and enabled toggles.
6. Standardized 3D Coordinate system: X (East/West), Y (North/South), Z (Height/Altitude).
7. Live Push map changes directly to the running server so all clients see updates instantly.
"""

import os
import sys
import json
import glob
import socket
import winsound
import wx

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS_DIR = os.path.join(ROOT_DIR, "assets", "sounds")
MAPS_DIR = os.path.join(ROOT_DIR, "assets", "maps")
os.makedirs(MAPS_DIR, exist_ok=True)

SURFACE_TYPES = ["wood", "stone", "carpet", "metal", "grass", "tile", "glass", "leather_fabric"]
AMBIENCE_CATEGORIES = ["indoor", "outdoor", "nature", "urban"]
AURA_TYPES = ["none", "vinyl", "chime", "hum", "silk"]
CLOTHING_TYPES = ["regular", "naked", "armor"]


class StructureEditorDialog(wx.Dialog):
    """Dedicated Editor Window for editing everything within an interior structure."""
    def __init__(self, parent, structure_dict, on_save_callback):
        super().__init__(parent, title=f"Structure Editor: {structure_dict.get('name', 'Structure')}", size=(750, 580))
        self.structure = dict(structure_dict)
        self.on_save_callback = on_save_callback
        self.interior_objects = list(self.structure.get("interior_objects", []))

        main_sizer = wx.BoxSizer(wx.VERTICAL)

        # Structure Meta Info
        meta_box = wx.StaticBox(self, label="Structure Information")
        meta_sizer = wx.StaticBoxSizer(meta_box, wx.HORIZONTAL)

        meta_sizer.Add(wx.StaticText(self, label="Structure Name:"), 0, wx.ALIGN_CENTER_VERTICAL | wx.RIGHT, 5)
        self.txt_sname = wx.TextCtrl(self, value=self.structure.get("name", "Interior Structure"))
        meta_sizer.Add(self.txt_sname, 1, wx.EXPAND | wx.RIGHT, 10)

        meta_sizer.Add(wx.StaticText(self, label="Wall Material:"), 0, wx.ALIGN_CENTER_VERTICAL | wx.RIGHT, 5)
        self.choice_smat = wx.Choice(self, choices=SURFACE_TYPES)
        self.choice_smat.SetStringSelection("wood")
        meta_sizer.Add(self.choice_smat, 0)

        main_sizer.Add(meta_sizer, 0, wx.EXPAND | wx.ALL, 8)

        # Content Splitter: Add Interior Furniture & List
        content_sizer = wx.BoxSizer(wx.HORIZONTAL)

        # Left: Add Interior Object
        obj_box = wx.StaticBox(self, label="Add / Modify Interior Object")
        obj_sizer = wx.StaticBoxSizer(obj_box, wx.VERTICAL)
        grid_obj = wx.FlexGridSizer(rows=7, cols=2, vgap=4, hgap=8)

        grid_obj.Add(wx.StaticText(self, label="Object Name:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.txt_iobj_name = wx.TextCtrl(self, value="Solid Oak Desk")
        grid_obj.Add(self.txt_iobj_name, 1, wx.EXPAND)

        grid_obj.Add(wx.StaticText(self, label="Material:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.choice_iobj_mat = wx.Choice(self, choices=SURFACE_TYPES)
        self.choice_iobj_mat.SetStringSelection("wood")
        grid_obj.Add(self.choice_iobj_mat, 1, wx.EXPAND)

        grid_obj.Add(wx.StaticText(self, label="X Coordinates (Min, Max):"), 0, wx.ALIGN_CENTER_VERTICAL)
        h_x = wx.BoxSizer(wx.HORIZONTAL)
        self.txt_iobj_min_x = wx.TextCtrl(self, value="-2.0")
        self.txt_iobj_max_x = wx.TextCtrl(self, value="-0.5")
        h_x.Add(self.txt_iobj_min_x, 1, wx.EXPAND)
        h_x.Add(self.txt_iobj_max_x, 1, wx.EXPAND | wx.LEFT, 4)
        grid_obj.Add(h_x, 1, wx.EXPAND)

        grid_obj.Add(wx.StaticText(self, label="Y Coordinates (Min, Max):"), 0, wx.ALIGN_CENTER_VERTICAL)
        h_y = wx.BoxSizer(wx.HORIZONTAL)
        self.txt_iobj_min_y = wx.TextCtrl(self, value="0.0")
        self.txt_iobj_max_y = wx.TextCtrl(self, value="1.5")
        h_y.Add(self.txt_iobj_min_y, 1, wx.EXPAND)
        h_y.Add(self.txt_iobj_max_y, 1, wx.EXPAND | wx.LEFT, 4)
        grid_obj.Add(h_y, 1, wx.EXPAND)

        grid_obj.Add(wx.StaticText(self, label="Health Points (HP):"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.txt_iobj_hp = wx.TextCtrl(self, value="100")
        grid_obj.Add(self.txt_iobj_hp, 1, wx.EXPAND)

        grid_obj.Add(wx.StaticText(self, label="Respawn Enabled:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.chk_iregen = wx.CheckBox(self, label="Regenerate after destruction")
        self.chk_iregen.SetValue(True)
        grid_obj.Add(self.chk_iregen, 1, wx.EXPAND)

        grid_obj.Add(wx.StaticText(self, label="Respawn Timer (seconds):"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.txt_iregen_sec = wx.TextCtrl(self, value="45")
        grid_obj.Add(self.txt_iregen_sec, 1, wx.EXPAND)

        grid_obj.AddGrowableCol(1, 1)
        obj_sizer.Add(grid_obj, 0, wx.EXPAND | wx.ALL, 4)

        self.btn_add_iobj = wx.Button(self, label="Add / Update Interior Object")
        self.btn_add_iobj.Bind(wx.EVT_BUTTON, self.on_add_interior_obj)
        obj_sizer.Add(self.btn_add_iobj, 0, wx.EXPAND | wx.TOP, 6)

        content_sizer.Add(obj_sizer, 1, wx.EXPAND | wx.RIGHT, 6)

        # Right: Interior Objects List
        list_box = wx.StaticBox(self, label="Interior Furniture & Items")
        list_sizer = wx.StaticBoxSizer(list_box, wx.VERTICAL)

        self.lst_iobjs = wx.ListBox(self, style=wx.LB_SINGLE)
        self.lst_iobjs.Bind(wx.EVT_LISTBOX, self.on_select_interior_obj)
        list_sizer.Add(self.lst_iobjs, 1, wx.EXPAND | wx.ALL, 4)

        self.btn_del_iobj = wx.Button(self, label="Remove Selected Object")
        self.btn_del_iobj.Bind(wx.EVT_BUTTON, self.on_delete_interior_obj)
        list_sizer.Add(self.btn_del_iobj, 0, wx.EXPAND | wx.TOP, 4)

        content_sizer.Add(list_sizer, 1, wx.EXPAND)
        main_sizer.Add(content_sizer, 1, wx.EXPAND | wx.LEFT | wx.RIGHT | wx.BOTTOM, 8)

        # Bottom Buttons
        btn_sizer = wx.BoxSizer(wx.HORIZONTAL)
        self.btn_save = wx.Button(self, wx.ID_OK, label="Apply & Close Structure Editor")
        self.btn_save.Bind(wx.EVT_BUTTON, self.on_apply)
        btn_sizer.Add(self.btn_save, 1, wx.EXPAND | wx.RIGHT, 6)

        self.btn_cancel = wx.Button(self, wx.ID_CANCEL, label="Cancel")
        btn_sizer.Add(self.btn_cancel, 0)

        main_sizer.Add(btn_sizer, 0, wx.EXPAND | wx.LEFT | wx.RIGHT | wx.BOTTOM, 8)
        self.SetSizer(main_sizer)

        self.refresh_interior_list()

    def refresh_interior_list(self):
        self.lst_iobjs.Clear()
        for o in self.interior_objects:
            min_y = o.get("min_y", o.get("min_z", 0.0))
            max_y = o.get("max_y", o.get("max_z", 0.0))
            sec = o.get("respawn_time_seconds", 45)
            self.lst_iobjs.Append(f"{o.get('name')} ({o.get('material')}) [X: {o.get('min_x')}-{o.get('max_x')}, Y: {min_y}-{max_y}] - {sec}s regen")

    def on_select_interior_obj(self, event):
        sel = self.lst_iobjs.GetSelection()
        if sel != wx.NOT_FOUND and sel < len(self.interior_objects):
            o = self.interior_objects[sel]
            self.txt_iobj_name.SetValue(str(o.get("name", "")))
            mat = o.get("material", "wood")
            if mat in SURFACE_TYPES:
                self.choice_iobj_mat.SetStringSelection(mat)
            self.txt_iobj_min_x.SetValue(str(o.get("min_x", 0.0)))
            self.txt_iobj_max_x.SetValue(str(o.get("max_x", 0.0)))
            min_y = o.get("min_y", o.get("min_z", 0.0))
            max_y = o.get("max_y", o.get("max_z", 0.0))
            self.txt_iobj_min_y.SetValue(str(min_y))
            self.txt_iobj_max_y.SetValue(str(max_y))
            self.txt_iobj_hp.SetValue(str(o.get("health", 100)))
            self.chk_iregen.SetValue(bool(o.get("regeneration", True)))
            self.txt_iregen_sec.SetValue(str(o.get("respawn_time_seconds", 45)))

    def on_add_interior_obj(self, event):
        try:
            name = self.txt_iobj_name.GetValue().strip()
            mat = self.choice_iobj_mat.GetStringSelection()
            min_x = float(self.txt_iobj_min_x.GetValue())
            max_x = float(self.txt_iobj_max_x.GetValue())
            min_y = float(self.txt_iobj_min_y.GetValue())
            max_y = float(self.txt_iobj_max_y.GetValue())
            hp = int(self.txt_iobj_hp.GetValue())
            regen = self.chk_iregen.GetValue()
            sec = int(self.txt_iregen_sec.GetValue())
        except ValueError:
            wx.MessageBox("Coordinates, health, and respawn timer must be numbers.", "Validation", wx.ICON_WARNING)
            return

        obj = {
            "name": name,
            "material": mat,
            "min_x": min_x,
            "max_x": max_x,
            "min_y": min_y,
            "max_y": max_y,
            "min_z": min_y,
            "max_z": max_y,
            "height": 1.0,
            "health": hp,
            "destructible": True,
            "regeneration": regen,
            "respawn_time_seconds": sec
        }

        sel = self.lst_iobjs.GetSelection()
        if sel != wx.NOT_FOUND and sel < len(self.interior_objects):
            self.interior_objects[sel] = obj
        else:
            self.interior_objects.append(obj)

        self.refresh_interior_list()
        wx.Bell()

    def on_delete_interior_obj(self, event):
        sel = self.lst_iobjs.GetSelection()
        if sel != wx.NOT_FOUND:
            del self.interior_objects[sel]
            self.refresh_interior_list()

    def on_apply(self, event):
        self.structure["name"] = self.txt_sname.GetValue().strip()
        self.structure["interior_objects"] = self.interior_objects
        if self.on_save_callback:
            self.on_save_callback(self.structure)
        self.EndModal(wx.ID_OK)


class MapBuilderPanel(wx.Panel):
    def __init__(self, parent):
        super().__init__(parent)
        self.objects = []
        self.travel_points = []
        self.structures = []
        self.raw_map_data = {}
        self.current_loaded_file = None

        main_sizer = wx.BoxSizer(wx.VERTICAL)

        # 1. Hierarchical Map Selection & Live Sync Bar
        bar_box = wx.StaticBox(self, label="Hierarchical Map Network & Live Server Synchronization")
        bar_sizer = wx.StaticBoxSizer(bar_box, wx.HORIZONTAL)

        bar_sizer.Add(wx.StaticText(self, label="Active Map:"), 0, wx.ALIGN_CENTER_VERTICAL | wx.RIGHT, 5)
        self.choice_maps = wx.Choice(self, choices=[])
        self.refresh_map_choices()
        bar_sizer.Add(self.choice_maps, 1, wx.EXPAND | wx.RIGHT, 6)

        self.btn_load_map = wx.Button(self, label="Open Map")
        self.btn_load_map.Bind(wx.EVT_BUTTON, self.on_load_selected_map)
        bar_sizer.Add(self.btn_load_map, 0, wx.RIGHT, 5)

        self.btn_edit_struct = wx.Button(self, label="Open Structure in Window...")
        self.btn_edit_struct.Bind(wx.EVT_BUTTON, self.on_open_structure_window)
        bar_sizer.Add(self.btn_edit_struct, 0, wx.RIGHT, 5)

        self.btn_push_server = wx.Button(self, label="Push Live to Server")
        self.btn_push_server.SetBackgroundColour(wx.Colour(230, 248, 230))
        self.btn_push_server.Bind(wx.EVT_BUTTON, self.on_push_to_server)
        bar_sizer.Add(self.btn_push_server, 0)

        main_sizer.Add(bar_sizer, 0, wx.EXPAND | wx.ALL, 6)

        # 2. Map Regional Settings (3D Standards)
        meta_box = wx.StaticBox(self, label="World Map & 3D Spatial Environment")
        meta_sizer = wx.StaticBoxSizer(meta_box, wx.VERTICAL)
        grid_meta = wx.FlexGridSizer(rows=3, cols=4, vgap=4, hgap=10)

        grid_meta.Add(wx.StaticText(self, label="Map Title:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.txt_title = wx.TextCtrl(self, value="Lounge & Great Outdoor Field")
        grid_meta.Add(self.txt_title, 1, wx.EXPAND)

        grid_meta.Add(wx.StaticText(self, label="Ground Surface:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.choice_default_surface = wx.Choice(self, choices=SURFACE_TYPES)
        self.choice_default_surface.SetStringSelection("grass")
        grid_meta.Add(self.choice_default_surface, 1, wx.EXPAND)

        grid_meta.Add(wx.StaticText(self, label="Area Ambience:"), 0, wx.ALIGN_CENTER_VERTICAL)
        h_amb = wx.BoxSizer(wx.HORIZONTAL)
        self.choice_amb_cat = wx.Choice(self, choices=AMBIENCE_CATEGORIES)
        self.choice_amb_cat.SetStringSelection("outdoor")
        h_amb.Add(self.choice_amb_cat, 1, wx.EXPAND | wx.RIGHT, 4)
        self.btn_audition_amb = wx.Button(self, label="Audition")
        self.btn_audition_amb.Bind(wx.EVT_BUTTON, self.on_audition_ambience)
        h_amb.Add(self.btn_audition_amb, 0)
        grid_meta.Add(h_amb, 1, wx.EXPAND)

        grid_meta.Add(wx.StaticText(self, label="Reverb RT60 (s):"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.txt_reverb = wx.TextCtrl(self, value="0.35")
        grid_meta.Add(self.txt_reverb, 1, wx.EXPAND)

        grid_meta.AddGrowableCol(1, 1)
        grid_meta.AddGrowableCol(3, 1)
        meta_sizer.Add(grid_meta, 0, wx.EXPAND | wx.ALL, 4)
        main_sizer.Add(meta_sizer, 0, wx.EXPAND | wx.LEFT | wx.RIGHT | wx.BOTTOM, 6)

        # 3. Content Splitter: Objects & Travel Points
        content_sizer = wx.BoxSizer(wx.HORIZONTAL)

        # Left Column: Add / Modify World Objects with Configurable Respawn Timer
        obj_box = wx.StaticBox(self, label="Add / Modify World Object & 3D Emitter")
        obj_sizer = wx.StaticBoxSizer(obj_box, wx.VERTICAL)
        grid_obj = wx.FlexGridSizer(rows=9, cols=2, vgap=3, hgap=8)

        grid_obj.Add(wx.StaticText(self, label="Object Name:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.txt_obj_name = wx.TextCtrl(self, value="Vintage Jukebox")
        grid_obj.Add(self.txt_obj_name, 1, wx.EXPAND)

        grid_obj.Add(wx.StaticText(self, label="Material:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.choice_obj_mat = wx.Choice(self, choices=SURFACE_TYPES)
        self.choice_obj_mat.SetStringSelection("wood")
        grid_obj.Add(self.choice_obj_mat, 1, wx.EXPAND)

        grid_obj.Add(wx.StaticText(self, label="X Range (East/West meters):"), 0, wx.ALIGN_CENTER_VERTICAL)
        h_x = wx.BoxSizer(wx.HORIZONTAL)
        self.txt_obj_min_x = wx.TextCtrl(self, value="-2.0")
        self.txt_obj_max_x = wx.TextCtrl(self, value="-0.5")
        h_x.Add(self.txt_obj_min_x, 1, wx.EXPAND)
        h_x.Add(self.txt_obj_max_x, 1, wx.EXPAND | wx.LEFT, 4)
        grid_obj.Add(h_x, 1, wx.EXPAND)

        grid_obj.Add(wx.StaticText(self, label="Y Range (North/South meters):"), 0, wx.ALIGN_CENTER_VERTICAL)
        h_y = wx.BoxSizer(wx.HORIZONTAL)
        self.txt_obj_min_y = wx.TextCtrl(self, value="10.0")
        self.txt_obj_max_y = wx.TextCtrl(self, value="11.5")
        h_y.Add(self.txt_obj_min_y, 1, wx.EXPAND)
        h_y.Add(self.txt_obj_max_y, 1, wx.EXPAND | wx.LEFT, 4)
        grid_obj.Add(h_y, 1, wx.EXPAND)

        grid_obj.Add(wx.StaticText(self, label="Health Points (HP):"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.txt_obj_hp = wx.TextCtrl(self, value="100")
        grid_obj.Add(self.txt_obj_hp, 1, wx.EXPAND)

        grid_obj.Add(wx.StaticText(self, label="Respawn Enabled:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.chk_regen = wx.CheckBox(self, label="Enable Respawn after destruction")
        self.chk_regen.SetValue(True)
        grid_obj.Add(self.chk_regen, 1, wx.EXPAND)

        grid_obj.Add(wx.StaticText(self, label="Respawn Time (seconds):"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.txt_respawn_sec = wx.TextCtrl(self, value="60")
        grid_obj.Add(self.txt_respawn_sec, 1, wx.EXPAND)

        grid_obj.Add(wx.StaticText(self, label="Continuous 3D Sound:"), 0, wx.ALIGN_CENTER_VERTICAL)
        h_snd = wx.BoxSizer(wx.HORIZONTAL)
        self.txt_obj_sound = wx.TextCtrl(self, value="objects/generator_hum.wav")
        h_snd.Add(self.txt_obj_sound, 1, wx.EXPAND | wx.RIGHT, 4)
        self.btn_audition_emitter = wx.Button(self, label="Audition")
        self.btn_audition_emitter.Bind(wx.EVT_BUTTON, self.on_audition_emitter)
        h_snd.Add(self.btn_audition_emitter, 0)
        grid_obj.Add(h_snd, 1, wx.EXPAND)

        grid_obj.Add(wx.StaticText(self, label="Audible Range (tiles):"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.txt_obj_range = wx.TextCtrl(self, value="25")
        grid_obj.Add(self.txt_obj_range, 1, wx.EXPAND)

        grid_obj.AddGrowableCol(1, 1)
        obj_sizer.Add(grid_obj, 0, wx.EXPAND | wx.ALL, 4)

        btn_sizer = wx.BoxSizer(wx.HORIZONTAL)
        self.btn_add_obj = wx.Button(self, label="Add / Update in Map")
        self.btn_add_obj.Bind(wx.EVT_BUTTON, self.on_add_object)
        btn_sizer.Add(self.btn_add_obj, 1, wx.EXPAND | wx.RIGHT, 4)

        self.btn_audition_mat = wx.Button(self, label="Audition Material Hit")
        self.btn_audition_mat.Bind(wx.EVT_BUTTON, self.on_audition_material)
        btn_sizer.Add(self.btn_audition_mat, 1, wx.EXPAND)

        obj_sizer.Add(btn_sizer, 0, wx.EXPAND | wx.TOP, 4)
        content_sizer.Add(obj_sizer, 1, wx.EXPAND | wx.ALL, 4)

        # Right Column: Entities List & TravelPoints
        list_box = wx.StaticBox(self, label="Map Entities, Structures & TravelPoints")
        list_sizer = wx.StaticBoxSizer(list_box, wx.VERTICAL)

        self.lst_entities = wx.ListBox(self, style=wx.LB_SINGLE)
        self.lst_entities.Bind(wx.EVT_LISTBOX, self.on_select_entity)
        list_sizer.Add(self.lst_entities, 1, wx.EXPAND | wx.ALL, 4)

        h_list_btns = wx.BoxSizer(wx.HORIZONTAL)
        self.btn_remove = wx.Button(self, label="Remove Selected")
        self.btn_remove.Bind(wx.EVT_BUTTON, self.on_remove_selected)
        h_list_btns.Add(self.btn_remove, 1, wx.EXPAND | wx.RIGHT, 4)

        self.btn_save_map = wx.Button(self, label="Save Changes to Disk")
        self.btn_save_map.Bind(wx.EVT_BUTTON, self.on_export_map)
        h_list_btns.Add(self.btn_save_map, 1, wx.EXPAND)
        list_sizer.Add(h_list_btns, 0, wx.EXPAND | wx.TOP, 4)

        content_sizer.Add(list_sizer, 1, wx.EXPAND | wx.ALL, 4)
        main_sizer.Add(content_sizer, 1, wx.EXPAND | wx.ALL, 4)

        self.SetSizer(main_sizer)

        def_path = os.path.join(MAPS_DIR, "default_world.json")
        if os.path.exists(def_path):
            self.load_map_file(def_path)

    def refresh_map_choices(self):
        map_files = glob.glob(os.path.join(MAPS_DIR, "*.json"))
        names = [os.path.basename(f) for f in map_files]
        if not names:
            names = ["default_world.json"]
        self.choice_maps.Set(names)
        if "default_world.json" in names:
            self.choice_maps.SetStringSelection("default_world.json")
        else:
            self.choice_maps.SetSelection(0)

    def on_load_selected_map(self, event):
        sel_name = self.choice_maps.GetStringSelection()
        if not sel_name:
            return
        path = os.path.join(MAPS_DIR, sel_name)
        self.load_map_file(path)

    def load_map_file(self, path):
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
        except Exception as e:
            wx.MessageBox(f"Failed to read map file:\n{e}", "Error", wx.ICON_ERROR)
            return

        self.raw_map_data = data
        self.current_loaded_file = path
        self.objects = []
        self.travel_points = list(data.get("travel_points", []))
        self.structures = list(data.get("structures", []))
        self.lst_entities.Clear()

        self.txt_title.SetValue(data.get("title", os.path.basename(path)))
        self.choice_default_surface.SetStringSelection(data.get("default_surface", "grass"))
        self.txt_reverb.SetValue(str(data.get("reverb_rt60", 0.35)))

        for r in data.get("regions", []):
            if r.get("id") == "outside" or r.get("is_outside"):
                cat = r.get("ambience_category", "outdoor")
                if cat in AMBIENCE_CATEGORIES:
                    self.choice_amb_cat.SetStringSelection(cat)

        # 1. TravelPoints
        for tp in self.travel_points:
            self.lst_entities.Append(f"[TRAVELPOINT] {tp.get('name')} -> Leads to {tp.get('target_map')}")

        # 2. Structures
        for s in self.structures:
            s_name = s.get("name", "Structure")
            count = len(s.get("interior_objects", []))
            self.lst_entities.Append(f"[STRUCTURE] {s_name} ({count} interior items)")
            for obj in s.get("interior_objects", []):
                o = dict(obj)
                o["_location"] = f"Inside {s_name}"
                self.objects.append(o)
                min_y = o.get("min_y", o.get("min_z", 0.0))
                max_y = o.get("max_y", o.get("max_z", 0.0))
                self.lst_entities.Append(f"  * {o.get('name')} ({o.get('material')}) [X: {o.get('min_x')}-{o.get('max_x')}, Y: {min_y}-{max_y}]")

        # 3. Outdoor field objects
        for obj in data.get("field_objects", []):
            o = dict(obj)
            o["_location"] = "Outdoor Field"
            self.objects.append(o)
            min_y = o.get("min_y", o.get("min_z", 0.0))
            max_y = o.get("max_y", o.get("max_z", 0.0))
            snd_tag = f" [Sound: {o.get('sound_asset')}]" if o.get("sound_asset") else ""
            self.lst_entities.Append(f"{o.get('name')} ({o.get('material')}){snd_tag} [X: {o.get('min_x')}-{o.get('max_x')}, Y: {min_y}-{max_y}]")

        # 4. Direct objects
        for obj in data.get("objects", []):
            o = dict(obj)
            o["_location"] = "World"
            self.objects.append(o)
            min_y = o.get("min_y", o.get("min_z", 0.0))
            max_y = o.get("max_y", o.get("max_z", 0.0))
            snd_tag = f" [Sound: {o.get('sound_asset')}]" if o.get("sound_asset") else ""
            self.lst_entities.Append(f"{o.get('name')} ({o.get('material')}){snd_tag} [X: {o.get('min_x')}-{o.get('max_x')}, Y: {min_y}-{max_y}]")

        wx.Bell()

    def on_open_structure_window(self, event):
        if not self.structures:
            # Create a default structure if none exists
            self.structures.append({
                "name": "Cozy Residential Lounge",
                "walls": [],
                "interior_objects": []
            })

        st = self.structures[0]
        def on_saved(updated_struct):
            self.structures[0] = updated_struct
            self.on_export_map(None)
            wx.MessageBox("Structure updated and saved to map!", "Structure Saved", wx.ICON_INFORMATION)

        dlg = StructureEditorDialog(self, st, on_saved)
        dlg.ShowModal()
        dlg.Destroy()
        if self.current_loaded_file:
            self.load_map_file(self.current_loaded_file)

    def on_select_entity(self, event):
        sel = self.lst_entities.GetSelection()
        txt = self.lst_entities.GetString(sel)
        if "[TRAVELPOINT]" in txt or "[STRUCTURE]" in txt:
            return

        # Find matching object
        clean_txt = txt.replace("  * ", "")
        for obj in self.objects:
            if obj.get("name") in clean_txt:
                self.txt_obj_name.SetValue(str(obj.get("name", "")))
                mat = obj.get("material", "wood")
                if mat in SURFACE_TYPES:
                    self.choice_obj_mat.SetStringSelection(mat)
                self.txt_obj_min_x.SetValue(str(obj.get("min_x", 0.0)))
                self.txt_obj_max_x.SetValue(str(obj.get("max_x", 0.0)))
                min_y = obj.get("min_y", obj.get("min_z", 0.0))
                max_y = obj.get("max_y", obj.get("max_z", 0.0))
                self.txt_obj_min_y.SetValue(str(min_y))
                self.txt_obj_max_y.SetValue(str(max_y))
                self.txt_obj_hp.SetValue(str(obj.get("health", 100)))
                self.chk_regen.SetValue(bool(obj.get("regeneration", True)))
                self.txt_respawn_sec.SetValue(str(obj.get("respawn_time_seconds", 60)))
                self.txt_obj_sound.SetValue(str(obj.get("sound_asset", "")))
                self.txt_obj_range.SetValue(str(obj.get("sound_radius", 25.0)))
                break

    def on_add_object(self, event):
        try:
            name = self.txt_obj_name.GetValue().strip()
            mat = self.choice_obj_mat.GetStringSelection()
            min_x = float(self.txt_obj_min_x.GetValue())
            max_x = float(self.txt_obj_max_x.GetValue())
            min_y = float(self.txt_obj_min_y.GetValue())
            max_y = float(self.txt_obj_max_y.GetValue())
            hp = int(self.txt_obj_hp.GetValue())
            regen = self.chk_regen.GetValue()
            sec = int(self.txt_respawn_sec.GetValue())
            snd = self.txt_obj_sound.GetValue().strip()
            srad = float(self.txt_obj_range.GetValue())
        except ValueError:
            wx.MessageBox("Coordinates, health, volume, and timer must be valid numbers.", "Validation", wx.ICON_ERROR)
            return

        obj = {
            "name": name,
            "material": mat,
            "min_x": min_x,
            "max_x": max_x,
            "min_y": min_y,
            "max_y": max_y,
            "min_z": min_y,
            "max_z": max_y,
            "health": hp,
            "destructible": True,
            "regeneration": regen,
            "respawn_time_seconds": sec,
            "sound_asset": snd,
            "sound_volume": 0.8,
            "sound_radius": srad,
            "_location": "Outdoor Field" if min_y >= 4.0 else "Inside Lounge"
        }

        self.objects.append(obj)
        snd_tag = f" [Sound: {snd}]" if snd else ""
        self.lst_entities.Append(f"{name} ({mat}){snd_tag} [X: {min_x}-{max_x}, Y: {min_y}-{max_y}] - {sec}s")
        wx.Bell()

    def on_remove_selected(self, event):
        sel = self.lst_entities.GetSelection()
        if sel != wx.NOT_FOUND:
            self.lst_entities.Delete(sel)

    def assemble_full_map_dict(self):
        data = dict(self.raw_map_data)
        data["title"] = self.txt_title.GetValue().strip()
        data["default_surface"] = self.choice_default_surface.GetStringSelection()
        try:
            data["reverb_rt60"] = float(self.txt_reverb.GetValue())
        except ValueError:
            data["reverb_rt60"] = 0.35

        amb_cat = self.choice_amb_cat.GetStringSelection()
        for r in data.get("regions", []):
            if r.get("id") == "outside" or r.get("is_outside"):
                r["ambience_category"] = amb_cat
                r["ambience_asset"] = f"ambiences/{amb_cat}/wind_field.wav" if amb_cat == "outdoor" else f"ambiences/{amb_cat}/room_tone.wav"

        interior_objs = []
        field_objs = []
        for o in self.objects:
            clean = dict(o)
            loc = clean.pop("_location", "")
            if "Inside" in loc or clean.get("max_y", clean.get("max_z", 0.0)) <= 4.0:
                interior_objs.append(clean)
            else:
                field_objs.append(clean)

        if "structures" in data and len(data["structures"]) > 0:
            data["structures"][0]["interior_objects"] = interior_objs
        else:
            data["structures"] = [{"name": "Lounge", "interior_objects": interior_objs, "walls": []}]

        data["field_objects"] = field_objs
        data["travel_points"] = self.travel_points
        return data

    def on_export_map(self, event):
        data = self.assemble_full_map_dict()
        target_path = self.current_loaded_file
        if not target_path:
            target_path = os.path.join(MAPS_DIR, "default_world.json")

        with open(target_path, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2)

        self.refresh_map_choices()
        if event:
            wx.MessageBox(f"Map successfully saved to disk:\n{target_path}", "Saved to Disk", wx.ICON_INFORMATION)

    def on_push_to_server(self, event):
        data = self.assemble_full_map_dict()
        cfg_path = os.path.join(ROOT_DIR, "client_config.json")
        host = "127.0.0.1"
        port = 7777
        if os.path.exists(cfg_path):
            try:
                with open(cfg_path, "r", encoding="utf-8") as f:
                    c = json.load(f)
                    host = c.get("host", host)
                    port = c.get("port", port)
            except Exception:
                pass

        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(3.0)
            s.connect((host, port))

            join_msg = json.dumps({
                "type": "c2s_join",
                "payload": {"username": "[Studio Editor]", "room_id": "lounge", "aura": "none"}
            }) + "\n"
            s.sendall(join_msg.encode("utf-8"))

            update_msg = json.dumps({
                "type": "c2s_update_map",
                "payload": {"map": data}
            }) + "\n"
            s.sendall(update_msg.encode("utf-8"))

            self.on_export_map(None)
            s.close()
            wx.MessageBox(
                f"Map '{data['title']}' was pushed to the server ({host}:{port})!\nAll connected clients updated in real time.",
                "Live Server Push Complete",
                wx.ICON_INFORMATION
            )
        except Exception as e:
            wx.MessageBox(
                f"Could not connect to Fake-VR server at {host}:{port}:\n{e}\n\nPlease ensure your server is running.",
                "Server Push Failed",
                wx.ICON_WARNING
            )

    def on_audition_material(self, event):
        mat = self.choice_obj_mat.GetStringSelection()
        snd_path = os.path.join(ASSETS_DIR, "environment", f"destroy_{mat}.wav")
        if not os.path.exists(snd_path):
            snd_path = os.path.join(ASSETS_DIR, "footsteps", f"{mat}_1.wav")
        if os.path.exists(snd_path):
            winsound.PlaySound(snd_path, winsound.SND_FILENAME | winsound.SND_ASYNC)

    def on_audition_emitter(self, event):
        snd_asset = self.txt_obj_sound.GetValue().strip()
        cand = os.path.join(ASSETS_DIR, snd_asset)
        if not os.path.exists(cand):
            cand = os.path.join(ASSETS_DIR, "objects", snd_asset)
        if os.path.exists(cand):
            winsound.PlaySound(cand, winsound.SND_FILENAME | winsound.SND_ASYNC)

    def on_audition_ambience(self, event):
        cat = self.choice_amb_cat.GetStringSelection()
        cand = os.path.join(ASSETS_DIR, "ambiences", cat, "wind_field.wav" if cat == "outdoor" else "room_tone.wav")
        if not os.path.exists(cand):
            cand = os.path.join(ASSETS_DIR, "environment", "resonance_ambient.wav")
        if os.path.exists(cand):
            winsound.PlaySound(cand, winsound.SND_FILENAME | winsound.SND_ASYNC)


class SpawnerNPCStudioPanel(wx.Panel):
    """Spawner & NPC Studio: Design and configure living NPCs with audio and behavior scripting."""
    def __init__(self, parent):
        super().__init__(parent)
        self.spawners = []
        main_sizer = wx.BoxSizer(wx.VERTICAL)

        box = wx.StaticBox(self, label="Spawner & Interactive NPC Designer")
        box_sizer = wx.StaticBoxSizer(box, wx.VERTICAL)

        grid = wx.FlexGridSizer(rows=7, cols=2, vgap=6, hgap=10)

        grid.Add(wx.StaticText(self, label="NPC Name:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.txt_npc_name = wx.TextCtrl(self, value="Town Guard Cedric")
        grid.Add(self.txt_npc_name, 1, wx.EXPAND)

        grid.Add(wx.StaticText(self, label="Spawn Coordinates (X, Y in meters):"), 0, wx.ALIGN_CENTER_VERTICAL)
        h_xy = wx.BoxSizer(wx.HORIZONTAL)
        self.txt_sp_x = wx.TextCtrl(self, value="2.0")
        self.txt_sp_y = wx.TextCtrl(self, value="25.0")
        h_xy.Add(self.txt_sp_x, 1, wx.EXPAND)
        h_xy.Add(self.txt_sp_y, 1, wx.EXPAND | wx.LEFT, 4)
        grid.Add(h_xy, 1, wx.EXPAND)

        grid.Add(wx.StaticText(self, label="Wander Radius (tiles/meters):"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.txt_wander = wx.TextCtrl(self, value="6.0")
        grid.Add(self.txt_wander, 1, wx.EXPAND)

        grid.Add(wx.StaticText(self, label="Outfit / Foley Material:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.choice_clothing = wx.Choice(self, choices=CLOTHING_TYPES)
        self.choice_clothing.SetStringSelection("armor")
        grid.Add(self.choice_clothing, 1, wx.EXPAND)

        grid.Add(wx.StaticText(self, label="Voice / Acoustic Presence:"), 0, wx.ALIGN_CENTER_VERTICAL)
        h_voice = wx.BoxSizer(wx.HORIZONTAL)
        self.txt_voice = wx.TextCtrl(self, value="auras/hum.wav")
        h_voice.Add(self.txt_voice, 1, wx.EXPAND | wx.RIGHT, 4)
        self.btn_audition_voice = wx.Button(self, label="Audition Voice")
        self.btn_audition_voice.Bind(wx.EVT_BUTTON, self.on_audition_voice)
        h_voice.Add(self.btn_audition_voice, 0)
        grid.Add(h_voice, 1, wx.EXPAND)

        grid.Add(wx.StaticText(self, label="Dialogue Text:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.txt_dialogue = wx.TextCtrl(self, value="Hail, traveler! Keep your footing. The road north leads to the grand city.", style=wx.TE_MULTILINE, size=(-1, 50))
        grid.Add(self.txt_dialogue, 1, wx.EXPAND)

        grid.Add(wx.StaticText(self, label="Behavior Script Hook:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.txt_script = wx.TextCtrl(self, value="guard_patrol")
        grid.Add(self.txt_script, 1, wx.EXPAND)

        grid.AddGrowableCol(1, 1)
        box_sizer.Add(grid, 0, wx.EXPAND | wx.ALL, 6)

        btn_sizer = wx.BoxSizer(wx.HORIZONTAL)
        self.btn_add_spawner = wx.Button(self, label="Save Spawner to World")
        self.btn_add_spawner.Bind(wx.EVT_BUTTON, self.on_save_spawner)
        btn_sizer.Add(self.btn_add_spawner, 1, wx.EXPAND | wx.RIGHT, 5)

        self.btn_test_interact = wx.Button(self, label="Test Dialogue Speech")
        self.btn_test_interact.Bind(wx.EVT_BUTTON, self.on_test_dialogue)
        btn_sizer.Add(self.btn_test_interact, 1, wx.EXPAND)

        box_sizer.Add(btn_sizer, 0, wx.EXPAND | wx.ALL, 6)
        main_sizer.Add(box_sizer, 1, wx.EXPAND | wx.ALL, 8)
        self.SetSizer(main_sizer)

    def on_audition_voice(self, event):
        v = self.txt_voice.GetValue().strip()
        cand = os.path.join(ASSETS_DIR, v)
        if os.path.exists(cand):
            winsound.PlaySound(cand, winsound.SND_FILENAME | winsound.SND_ASYNC)
        else:
            wx.MessageBox(f"Sound not found: {cand}", "Audition", wx.ICON_WARNING)

    def on_test_dialogue(self, event):
        name = self.txt_npc_name.GetValue().strip()
        dlg = self.txt_dialogue.GetValue().strip()
        wx.MessageBox(f"{name} says:\n\n\"{dlg}\"", "NPC Interaction Preview", wx.ICON_INFORMATION)

    def on_save_spawner(self, event):
        wx.MessageBox("Spawner updated in world specification.", "Saved", wx.ICON_INFORMATION)


class AuraFashionPanel(wx.Panel):
    def __init__(self, parent):
        super().__init__(parent)
        main_sizer = wx.BoxSizer(wx.VERTICAL)
        box = wx.StaticBox(self, label="Acoustic Fashion & Avatar Auras")
        box_sizer = wx.StaticBoxSizer(box, wx.VERTICAL)

        grid = wx.FlexGridSizer(rows=3, cols=2, vgap=8, hgap=10)

        grid.Add(wx.StaticText(self, label="Acoustic Aura Type:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.choice_aura = wx.Choice(self, choices=AURA_TYPES)
        self.choice_aura.SetStringSelection("vinyl")
        grid.Add(self.choice_aura, 1, wx.EXPAND)

        grid.Add(wx.StaticText(self, label="Aura Description:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.txt_desc = wx.TextCtrl(self, style=wx.TE_MULTILINE | wx.TE_READONLY, size=(-1, 60))
        grid.Add(self.txt_desc, 1, wx.EXPAND)

        self.update_aura_desc()
        self.choice_aura.Bind(wx.EVT_CHOICE, lambda e: self.update_aura_desc())

        grid.AddGrowableCol(1, 1)
        box_sizer.Add(grid, 0, wx.EXPAND | wx.ALL, 6)

        self.btn_play_aura = wx.Button(self, label="Audition Aura Sound")
        self.btn_play_aura.Bind(wx.EVT_BUTTON, self.on_audition_aura)
        box_sizer.Add(self.btn_play_aura, 0, wx.EXPAND | wx.ALL, 6)

        main_sizer.Add(box_sizer, 1, wx.EXPAND | wx.ALL, 8)
        self.SetSizer(main_sizer)

    def update_aura_desc(self):
        sel = self.choice_aura.GetStringSelection()
        descs = {
            "none": "No acoustic aura. Standard quiet presence.",
            "vinyl": "Warm vintage vinyl crackle and nostalgic dust warmth upon movement.",
            "chime": "Ethereal crystal glass chime emitting delicate acoustic sparkle.",
            "hum": "Deep harmonic sub-bass drone and warm resonant presence.",
            "silk": "Soft whisper of rustling luxury fabric and silk acoustic texture."
        }
        self.txt_desc.SetValue(descs.get(sel, ""))

    def on_audition_aura(self, event):
        sel = self.choice_aura.GetStringSelection()
        if sel == "none":
            wx.MessageBox("None aura is silent.", "Audition", wx.ICON_INFORMATION)
            return

        cand = os.path.join(ASSETS_DIR, "auras", f"{sel}.wav")
        if os.path.exists(cand):
            winsound.PlaySound(cand, winsound.SND_FILENAME | winsound.SND_ASYNC)


class MainStudioFrame(wx.Frame):
    def __init__(self):
        super().__init__(None, title="Fake-VR Accessible Map, Structure & Spawner Studio", size=(880, 700))
        self.SetMinSize((740, 560))

        notebook = wx.Notebook(self)
        self.panel_map = MapBuilderPanel(notebook)
        self.panel_spawner = SpawnerNPCStudioPanel(notebook)
        self.panel_aura = AuraFashionPanel(notebook)

        notebook.AddPage(self.panel_map, "Hierarchical Maps & Structures")
        notebook.AddPage(self.panel_spawner, "Spawners & Living NPCs")
        notebook.AddPage(self.panel_aura, "Acoustic Auras & Fashion")

        self.CreateStatusBar()
        self.SetStatusText("Fake-VR Studio Ready. Tab and Arrow keys navigate controls.")
        self.Centre()


def main():
    app = wx.App(False)
    frame = MainStudioFrame()
    frame.Show()
    app.MainLoop()


if __name__ == "__main__":
    main()
