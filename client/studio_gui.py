"""
Fake-VR Accessible Map, Foley & Acoustic Studio (wxPython)
Allows visually impaired and sighted creators to:
1. View, Load, and Edit existing maps (such as default_world.json).
2. Configure Spatial Object Emitters (objects that hum, drip, rattle, or chime in 3D space).
3. Assign Area Ambience Subcategories (Indoor Room Tone, Outdoor Wind, Nature, Urban).
4. Live Push map changes directly to the running server so all clients see updates instantly.
5. Audition all sounds and preview Acoustic Auras (Vinyl, Crystal Chime, Sub-bass Hum, Silk).
6. Design Outfits & Foley Accessories with automatic variant discovery.
"""

import os
import sys
import json
import glob
import socket
import winsound
import wx

# Base paths
ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS_DIR = os.path.join(ROOT_DIR, "assets", "sounds")
MAPS_DIR = os.path.join(ROOT_DIR, "assets", "maps")
os.makedirs(MAPS_DIR, exist_ok=True)

SURFACE_TYPES = ["wood", "stone", "carpet", "metal", "grass", "tile", "glass", "leather_fabric"]
AMBIENCE_CATEGORIES = ["indoor", "outdoor", "nature", "urban"]
AURA_TYPES = ["none", "vinyl", "chime", "hum", "silk"]


class MapBuilderPanel(wx.Panel):
    def __init__(self, parent):
        super().__init__(parent)
        self.objects = []
        self.raw_map_data = {}
        self.current_loaded_file = None

        main_sizer = wx.BoxSizer(wx.VERTICAL)

        # 1. Map File & Server Synchronization Bar
        bar_box = wx.StaticBox(self, label="Map Preset & Live Server Synchronization")
        bar_sizer = wx.StaticBoxSizer(bar_box, wx.HORIZONTAL)

        bar_sizer.Add(wx.StaticText(self, label="Map Preset:"), 0, wx.ALIGN_CENTER_VERTICAL | wx.RIGHT, 5)
        self.choice_maps = wx.Choice(self, choices=[])
        self.refresh_map_choices()
        bar_sizer.Add(self.choice_maps, 1, wx.EXPAND | wx.RIGHT, 5)

        self.btn_load_map = wx.Button(self, label="Load Map")
        self.btn_load_map.Bind(wx.EVT_BUTTON, self.on_load_selected_map)
        bar_sizer.Add(self.btn_load_map, 0, wx.RIGHT, 5)

        self.btn_push_server = wx.Button(self, label="Push Live to Server")
        self.btn_push_server.SetBackgroundColour(wx.Colour(230, 245, 230))
        self.btn_push_server.Bind(wx.EVT_BUTTON, self.on_push_to_server)
        bar_sizer.Add(self.btn_push_server, 0)

        main_sizer.Add(bar_sizer, 0, wx.EXPAND | wx.ALL, 6)

        # 2. Map & Area Ambience Settings
        meta_box = wx.StaticBox(self, label="World Map & Regional Ambience")
        meta_sizer = wx.StaticBoxSizer(meta_box, wx.VERTICAL)
        grid_meta = wx.FlexGridSizer(rows=4, cols=2, vgap=4, hgap=10)

        grid_meta.Add(wx.StaticText(self, label="Map Title:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.txt_title = wx.TextCtrl(self, value="Lounge & Great Outdoor Field")
        grid_meta.Add(self.txt_title, 1, wx.EXPAND)

        grid_meta.Add(wx.StaticText(self, label="Default Ground Surface:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.choice_default_surface = wx.Choice(self, choices=SURFACE_TYPES)
        self.choice_default_surface.SetStringSelection("grass")
        grid_meta.Add(self.choice_default_surface, 1, wx.EXPAND)

        grid_meta.Add(wx.StaticText(self, label="Outside Area Ambience:"), 0, wx.ALIGN_CENTER_VERTICAL)
        h_amb = wx.BoxSizer(wx.HORIZONTAL)
        self.choice_amb_cat = wx.Choice(self, choices=AMBIENCE_CATEGORIES)
        self.choice_amb_cat.SetStringSelection("outdoor")
        h_amb.Add(self.choice_amb_cat, 1, wx.EXPAND | wx.RIGHT, 5)

        self.btn_audition_amb = wx.Button(self, label="Audition Ambience")
        self.btn_audition_amb.Bind(wx.EVT_BUTTON, self.on_audition_ambience)
        h_amb.Add(self.btn_audition_amb, 0)
        grid_meta.Add(h_amb, 1, wx.EXPAND)

        grid_meta.Add(wx.StaticText(self, label="Reverb Decay Time (RT60 seconds):"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.txt_reverb = wx.TextCtrl(self, value="0.35")
        grid_meta.Add(self.txt_reverb, 1, wx.EXPAND)

        grid_meta.AddGrowableCol(1, 1)
        meta_sizer.Add(grid_meta, 0, wx.EXPAND | wx.ALL, 4)
        main_sizer.Add(meta_sizer, 0, wx.EXPAND | wx.LEFT | wx.RIGHT | wx.BOTTOM, 6)

        # 3. Content Splitter: Object Creator and Entity List
        content_sizer = wx.BoxSizer(wx.HORIZONTAL)

        # Left Column: Add / Modify World Objects & Sound Emitters
        obj_box = wx.StaticBox(self, label="Add / Modify Object & 3D Emitter")
        obj_sizer = wx.StaticBoxSizer(obj_box, wx.VERTICAL)
        grid_obj = wx.FlexGridSizer(rows=9, cols=2, vgap=3, hgap=8)

        grid_obj.Add(wx.StaticText(self, label="Object Name:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.txt_obj_name = wx.TextCtrl(self, value="Vintage Jukebox")
        grid_obj.Add(self.txt_obj_name, 1, wx.EXPAND)

        grid_obj.Add(wx.StaticText(self, label="Material:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.choice_obj_mat = wx.Choice(self, choices=SURFACE_TYPES)
        self.choice_obj_mat.SetStringSelection("wood")
        grid_obj.Add(self.choice_obj_mat, 1, wx.EXPAND)

        grid_obj.Add(wx.StaticText(self, label="X Coordinates (Min, Max in meters):"), 0, wx.ALIGN_CENTER_VERTICAL)
        h_x = wx.BoxSizer(wx.HORIZONTAL)
        self.txt_obj_min_x = wx.TextCtrl(self, value="-2.0")
        self.txt_obj_max_x = wx.TextCtrl(self, value="-0.5")
        h_x.Add(self.txt_obj_min_x, 1, wx.EXPAND)
        h_x.Add(self.txt_obj_max_x, 1, wx.EXPAND | wx.LEFT, 4)
        grid_obj.Add(h_x, 1, wx.EXPAND)

        grid_obj.Add(wx.StaticText(self, label="Z Coordinates (Min, Max in meters):"), 0, wx.ALIGN_CENTER_VERTICAL)
        h_z = wx.BoxSizer(wx.HORIZONTAL)
        self.txt_obj_min_z = wx.TextCtrl(self, value="10.0")
        self.txt_obj_max_z = wx.TextCtrl(self, value="11.5")
        h_z.Add(self.txt_obj_min_z, 1, wx.EXPAND)
        h_z.Add(self.txt_obj_max_z, 1, wx.EXPAND | wx.LEFT, 4)
        grid_obj.Add(h_z, 1, wx.EXPAND)

        grid_obj.Add(wx.StaticText(self, label="Health Points (HP):"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.txt_obj_hp = wx.TextCtrl(self, value="100")
        grid_obj.Add(self.txt_obj_hp, 1, wx.EXPAND)

        grid_obj.Add(wx.StaticText(self, label="Continuous Sound Emitter:"), 0, wx.ALIGN_CENTER_VERTICAL)
        h_snd = wx.BoxSizer(wx.HORIZONTAL)
        self.txt_obj_sound = wx.TextCtrl(self, value="objects/generator_hum.wav")
        h_snd.Add(self.txt_obj_sound, 1, wx.EXPAND | wx.RIGHT, 4)
        self.btn_audition_emitter = wx.Button(self, label="Audition")
        self.btn_audition_emitter.Bind(wx.EVT_BUTTON, self.on_audition_emitter)
        h_snd.Add(self.btn_audition_emitter, 0)
        grid_obj.Add(h_snd, 1, wx.EXPAND)

        grid_obj.Add(wx.StaticText(self, label="Emitter Volume (0.1 to 1.0):"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.txt_obj_vol = wx.TextCtrl(self, value="0.75")
        grid_obj.Add(self.txt_obj_vol, 1, wx.EXPAND)

        grid_obj.Add(wx.StaticText(self, label="Regeneration:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.chk_regen = wx.CheckBox(self, label="1-Minute Respawn enabled")
        self.chk_regen.SetValue(True)
        grid_obj.Add(self.chk_regen, 1, wx.EXPAND)

        grid_obj.AddGrowableCol(1, 1)
        obj_sizer.Add(grid_obj, 0, wx.EXPAND | wx.ALL, 4)

        btn_sizer = wx.BoxSizer(wx.HORIZONTAL)
        self.btn_audition_mat = wx.Button(self, label="Audition Material Hit")
        self.btn_audition_mat.Bind(wx.EVT_BUTTON, self.on_audition_material)
        btn_sizer.Add(self.btn_audition_mat, 1, wx.EXPAND | wx.RIGHT, 4)

        self.btn_add_obj = wx.Button(self, label="Add / Update in Map")
        self.btn_add_obj.Bind(wx.EVT_BUTTON, self.on_add_object)
        btn_sizer.Add(self.btn_add_obj, 1, wx.EXPAND)
        obj_sizer.Add(btn_sizer, 0, wx.EXPAND | wx.TOP, 6)

        content_sizer.Add(obj_sizer, 1, wx.EXPAND | wx.ALL, 4)

        # Right Column: List of Placed Items & Export
        list_box = wx.StaticBox(self, label="Map Entities & Sound Beacons")
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

        # Auto-load default_world.json on startup if present
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
        self.lst_entities.Clear()

        self.txt_title.SetValue(data.get("title", os.path.basename(path)))
        self.choice_default_surface.SetStringSelection(data.get("default_surface", "grass"))
        self.txt_reverb.SetValue(str(data.get("reverb_rt60", 0.35)))

        # Ambience category check from regions
        for r in data.get("regions", []):
            if r.get("id") == "outside" or r.get("is_outside"):
                cat = r.get("ambience_category", "outdoor")
                if cat in AMBIENCE_CATEGORIES:
                    self.choice_amb_cat.SetStringSelection(cat)

        # 1. Structures interior objects (e.g. house furniture)
        for s in data.get("structures", []):
            s_name = s.get("name", "Structure")
            for obj in s.get("interior_objects", []):
                o = dict(obj)
                o["_location"] = f"Inside {s_name}"
                self.objects.append(o)
                snd_tag = f" [Sound: {o.get('sound_asset')}]" if o.get("sound_asset") else ""
                self.lst_entities.Append(
                    f"{o.get('name')} ({o.get('material')}){snd_tag} [X: {o.get('min_x')} to {o.get('max_x')}, Z: {o.get('min_z')} to {o.get('max_z')}]"
                )

        # 2. Outdoor field objects
        for obj in data.get("field_objects", []):
            o = dict(obj)
            o["_location"] = "Outdoor Field"
            self.objects.append(o)
            snd_tag = f" [Sound: {o.get('sound_asset')}]" if o.get("sound_asset") else ""
            self.lst_entities.Append(
                f"{o.get('name')} ({o.get('material')}){snd_tag} [X: {o.get('min_x')} to {o.get('max_x')}, Z: {o.get('min_z')} to {o.get('max_z')}]"
            )

        # 3. Direct objects list
        for obj in data.get("objects", []):
            o = dict(obj)
            o["_location"] = "World"
            self.objects.append(o)
            snd_tag = f" [Sound: {o.get('sound_asset')}]" if o.get("sound_asset") else ""
            self.lst_entities.Append(
                f"{o.get('name')} ({o.get('material')}){snd_tag} [X: {o.get('min_x')} to {o.get('max_x')}, Z: {o.get('min_z')} to {o.get('max_z')}]"
            )

        wx.Bell()

    def on_select_entity(self, event):
        sel = self.lst_entities.GetSelection()
        if sel != wx.NOT_FOUND and sel < len(self.objects):
            obj = self.objects[sel]
            self.txt_obj_name.SetValue(str(obj.get("name", "")))
            mat = obj.get("material", "wood")
            if mat in SURFACE_TYPES:
                self.choice_obj_mat.SetStringSelection(mat)
            self.txt_obj_min_x.SetValue(str(obj.get("min_x", 0.0)))
            self.txt_obj_max_x.SetValue(str(obj.get("max_x", 0.0)))
            self.txt_obj_min_z.SetValue(str(obj.get("min_z", 0.0)))
            self.txt_obj_max_z.SetValue(str(obj.get("max_z", 0.0)))
            self.txt_obj_hp.SetValue(str(obj.get("health", 100)))
            self.txt_obj_sound.SetValue(str(obj.get("sound_asset", "")))
            self.txt_obj_vol.SetValue(str(obj.get("sound_volume", 0.75)))
            self.chk_regen.SetValue(bool(obj.get("regeneration", True)))

    def on_audition_material(self, event):
        mat = self.choice_obj_mat.GetStringSelection()
        snd_path = os.path.join(ASSETS_DIR, "environment", f"destroy_{mat}.wav")
        if not os.path.exists(snd_path):
            snd_path = os.path.join(ASSETS_DIR, "footsteps", f"{mat}_1.wav")
        if not os.path.exists(snd_path):
            snd_path = os.path.join(ASSETS_DIR, "footsteps", f"{mat}.wav")

        if os.path.exists(snd_path):
            winsound.PlaySound(snd_path, winsound.SND_FILENAME | winsound.SND_ASYNC)
        else:
            wx.MessageBox(f"No WAV asset found for '{mat}' at {snd_path}", "Sound Not Found", wx.ICON_WARNING)

    def on_audition_emitter(self, event):
        snd_asset = self.txt_obj_sound.GetValue().strip()
        if not snd_asset:
            wx.MessageBox("Please enter a sound asset path (e.g. objects/generator_hum.wav).", "Audition", wx.ICON_INFORMATION)
            return

        cand = os.path.join(ASSETS_DIR, snd_asset)
        if not os.path.exists(cand):
            cand = os.path.join(ASSETS_DIR, "objects", snd_asset)
        if not os.path.exists(cand):
            cand = os.path.join(ASSETS_DIR, "auras", snd_asset)

        if os.path.exists(cand):
            winsound.PlaySound(cand, winsound.SND_FILENAME | winsound.SND_ASYNC)
        else:
            wx.MessageBox(f"Cannot locate sound asset:\n{cand}", "Not Found", wx.ICON_WARNING)

    def on_audition_ambience(self, event):
        cat = self.choice_amb_cat.GetStringSelection()
        cand = os.path.join(ASSETS_DIR, "ambiences", cat, "wind_field.wav" if cat == "outdoor" else "room_tone.wav")
        if not os.path.exists(cand):
            cand = os.path.join(ASSETS_DIR, "environment", "resonance_ambient.wav")

        if os.path.exists(cand):
            winsound.PlaySound(cand, winsound.SND_FILENAME | winsound.SND_ASYNC)
        else:
            wx.MessageBox("No ambience file found for category.", "Audition", wx.ICON_INFORMATION)

    def on_add_object(self, event):
        name = self.txt_obj_name.GetValue().strip()
        mat = self.choice_obj_mat.GetStringSelection()
        try:
            min_x = float(self.txt_obj_min_x.GetValue())
            max_x = float(self.txt_obj_max_x.GetValue())
            min_z = float(self.txt_obj_min_z.GetValue())
            max_z = float(self.txt_obj_max_z.GetValue())
            hp = int(self.txt_obj_hp.GetValue())
            vol = float(self.txt_obj_vol.GetValue())
        except ValueError:
            wx.MessageBox("Coordinates, Health, and Volume must be valid numbers.", "Input Error", wx.ICON_ERROR)
            return

        snd_asset = self.txt_obj_sound.GetValue().strip()

        obj = {
            "name": name,
            "material": mat,
            "min_x": min_x,
            "max_x": max_x,
            "min_z": min_z,
            "max_z": max_z,
            "health": hp,
            "sound_asset": snd_asset,
            "sound_volume": vol,
            "regeneration": self.chk_regen.GetValue(),
            "_location": "Outdoor Field" if min_z >= 4.0 else "Inside Lounge"
        }

        # If selected, update existing entity
        sel = self.lst_entities.GetSelection()
        snd_tag = f" [Sound: {snd_asset}]" if snd_asset else ""
        label = f"{name} ({mat}){snd_tag} [X: {min_x} to {max_x}, Z: {min_z} to {max_z}]"

        if sel != wx.NOT_FOUND and sel < len(self.objects):
            self.objects[sel] = obj
            self.lst_entities.SetString(sel, label)
        else:
            self.objects.append(obj)
            self.lst_entities.Append(label)

        wx.Bell()

    def on_remove_selected(self, event):
        sel = self.lst_entities.GetSelection()
        if sel != wx.NOT_FOUND:
            del self.objects[sel]
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
        # Update regional ambience
        for r in data.get("regions", []):
            if r.get("id") == "outside" or r.get("is_outside"):
                r["ambience_category"] = amb_cat
                r["ambience_asset"] = f"ambiences/{amb_cat}/wind_field.wav" if amb_cat == "outdoor" else f"ambiences/{amb_cat}/room_tone.wav"
                r["ambience_volume"] = 0.45

        interior_objs = []
        field_objs = []
        for o in self.objects:
            clean = dict(o)
            loc = clean.pop("_location", "")
            if "Inside" in loc or clean.get("max_z", 0.0) <= 4.0:
                interior_objs.append(clean)
            else:
                field_objs.append(clean)

        # Update structures interior objects
        if "structures" in data and len(data["structures"]) > 0:
            data["structures"][0]["interior_objects"] = interior_objs
        else:
            data["field_objects"] = field_objs + interior_objs
            return data

        data["field_objects"] = field_objs
        return data

    def on_export_map(self, event):
        data = self.assemble_full_map_dict()
        target_path = self.current_loaded_file
        if not target_path:
            title = data["title"]
            safe_name = "".join(c for c in title if c.isalnum() or c in (' ', '_', '-')).strip().replace(' ', '_').lower()
            target_path = os.path.join(MAPS_DIR, f"{safe_name}.json")

        with open(target_path, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2)

        self.refresh_map_choices()
        wx.MessageBox(f"Map successfully saved to disk:\n{target_path}", "Saved to Disk", wx.ICON_INFORMATION)

    def on_push_to_server(self, event):
        """Pushes current map configuration directly to the active server over TCP socket."""
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

            # 1. Handshake as Studio Editor
            join_msg = json.dumps({
                "type": "c2s_join",
                "payload": {"username": "[Studio Editor]", "room_id": "lounge", "aura": "none"}
            }) + "\n"
            s.sendall(join_msg.encode("utf-8"))

            # 2. Push update map packet
            update_msg = json.dumps({
                "type": "c2s_update_map",
                "payload": {"map": data}
            }) + "\n"
            s.sendall(update_msg.encode("utf-8"))

            # Save locally as well
            self.on_export_map(None)
            s.close()
            wx.MessageBox(
                f"Map '{data['title']}' was pushed to the server ({host}:{port})!\nAll connected clients have received the live map updates.",
                "Live Push Complete",
                wx.ICON_INFORMATION
            )
        except Exception as e:
            wx.MessageBox(
                f"Could not connect to Fake-VR server at {host}:{port}:\n{e}\n\nPlease ensure your server is running.",
                "Server Push Failed",
                wx.ICON_WARNING
            )


class AuraFashionPanel(wx.Panel):
    """Acoustic Fashion Studio for Auditioning & Customizing Psychoacoustic Auras."""
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

        btn_sizer = wx.BoxSizer(wx.HORIZONTAL)
        self.btn_play_aura = wx.Button(self, label="Audition Aura Sound")
        self.btn_play_aura.Bind(wx.EVT_BUTTON, self.on_audition_aura)
        btn_sizer.Add(self.btn_play_aura, 1, wx.EXPAND | wx.RIGHT, 5)

        box_sizer.Add(btn_sizer, 0, wx.EXPAND | wx.ALL, 6)
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
        else:
            wx.MessageBox(f"File not found: {cand}", "Error", wx.ICON_WARNING)


class FoleyStudioPanel(wx.Panel):
    def __init__(self, parent):
        super().__init__(parent)
        main_sizer = wx.BoxSizer(wx.VERTICAL)

        box = wx.StaticBox(self, label="Clothing & Foley Accessory Designer")
        box_sizer = wx.StaticBoxSizer(box, wx.VERTICAL)

        grid = wx.FlexGridSizer(rows=4, cols=2, vgap=6, hgap=10)

        grid.Add(wx.StaticText(self, label="Accessory / Outfit Name:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.txt_outfit_name = wx.TextCtrl(self, value="Tactical Dogtags")
        grid.Add(self.txt_outfit_name, 1, wx.EXPAND)

        grid.Add(wx.StaticText(self, label="Sound Asset Keyword:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.txt_keyword = wx.TextCtrl(self, value="dogtags")
        grid.Add(self.txt_keyword, 1, wx.EXPAND)

        grid.Add(wx.StaticText(self, label="Acoustic Volume (0.0 to 1.5):"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.slider_vol = wx.Slider(self, value=75, minValue=0, maxValue=150, style=wx.SL_HORIZONTAL | wx.SL_LABELS)
        grid.Add(self.slider_vol, 1, wx.EXPAND)

        grid.Add(wx.StaticText(self, label="Scan Sound Assets:"), 0, wx.ALIGN_CENTER_VERTICAL)
        self.btn_scan = wx.Button(self, label="Locate Sound & 4-Way Variants")
        self.btn_scan.Bind(wx.EVT_BUTTON, self.on_scan_assets)
        grid.Add(self.btn_scan, 1, wx.EXPAND)

        grid.AddGrowableCol(1, 1)
        box_sizer.Add(grid, 0, wx.EXPAND | wx.ALL, 5)

        # Discovered variants list
        list_box = wx.StaticBox(self, label="Located Sound Files")
        list_sizer = wx.StaticBoxSizer(list_box, wx.VERTICAL)
        self.lst_variants = wx.ListBox(self, style=wx.LB_SINGLE)
        list_sizer.Add(self.lst_variants, 1, wx.EXPAND | wx.ALL, 5)

        h_btn_sizer = wx.BoxSizer(wx.HORIZONTAL)
        self.btn_audition = wx.Button(self, label="Audition Selected Variant")
        self.btn_audition.Bind(wx.EVT_BUTTON, self.on_audition)
        h_btn_sizer.Add(self.btn_audition, 1, wx.EXPAND | wx.RIGHT, 5)

        list_sizer.Add(h_btn_sizer, 0, wx.EXPAND | wx.TOP, 5)
        box_sizer.Add(list_sizer, 1, wx.EXPAND | wx.ALL, 5)

        main_sizer.Add(box_sizer, 1, wx.EXPAND | wx.ALL, 8)
        self.SetSizer(main_sizer)

        self.discovered_files = []

    def on_scan_assets(self, event):
        keyword = self.txt_keyword.GetValue().strip().lower()
        if not keyword:
            wx.MessageBox("Please enter a sound keyword (e.g. dogtags, leather, armor).", "Error", wx.ICON_WARNING)
            return

        self.lst_variants.Clear()
        self.discovered_files.clear()

        # Search recursively in assets/sounds/
        pattern = os.path.join(ASSETS_DIR, "**", f"*{keyword}*.wav")
        found = glob.glob(pattern, recursive=True)

        if found:
            for f in found:
                self.discovered_files.append(f)
                rel = os.path.relpath(f, ROOT_DIR)
                self.lst_variants.Append(f"[Found Asset]: {rel}")
        else:
            self.lst_variants.Append(f"No WAV files containing '{keyword}' found in assets/sounds/")
            for i in range(1, 5):
                self.lst_variants.Append(f"[Placeholder Expected]: assets/sounds/foley/{keyword}/{keyword}_{i}.wav")

        wx.Bell()

    def on_audition(self, event):
        sel = self.lst_variants.GetSelection()
        if sel != wx.NOT_FOUND and sel < len(self.discovered_files):
            file_to_play = self.discovered_files[sel]
            winsound.PlaySound(file_to_play, winsound.SND_FILENAME | winsound.SND_ASYNC)
        elif self.discovered_files:
            winsound.PlaySound(self.discovered_files[0], winsound.SND_FILENAME | winsound.SND_ASYNC)
        else:
            wx.MessageBox("No playable sound file found for this keyword yet.", "Audition", wx.ICON_INFORMATION)


class MainStudioFrame(wx.Frame):
    def __init__(self):
        super().__init__(None, title="Fake-VR Accessible Map, Foley & Acoustic Studio", size=(840, 680))
        self.SetMinSize((700, 540))

        # Accessible Notebook with 3 dedicated tabs
        notebook = wx.Notebook(self)
        self.panel_map = MapBuilderPanel(notebook)
        self.panel_aura = AuraFashionPanel(notebook)
        self.panel_foley = FoleyStudioPanel(notebook)

        notebook.AddPage(self.panel_map, "Map & 3D Sound Emitters")
        notebook.AddPage(self.panel_aura, "Acoustic Auras & Fashion")
        notebook.AddPage(self.panel_foley, "Clothing & Foley Accessory Designer")

        # Status Bar
        self.CreateStatusBar()
        self.SetStatusText("Ready. Use Tab and Arrow keys to navigate between controls.")
        self.Centre()


def main():
    app = wx.App(False)
    frame = MainStudioFrame()
    frame.Show()
    app.MainLoop()


if __name__ == "__main__":
    main()
