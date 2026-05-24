TOOL.Category = "Render"
TOOL.Name = "Color - Submaterial TF2 Painter"
TOOL.Command = nil
TOOL.ConfigName = "" 
 
TOOL.ClientConVar["r"] = "45"
TOOL.ClientConVar["g"] = "45"
TOOL.ClientConVar["b"] = "36"
TOOL.ClientConVar["whichlist"] = "original"
TOOL.ClientConVar[ "effectcompatibility" ] = "1"

--Pulled from Submaterial itself
TOOL.ClientConVar[ "override" ] = "debug/env_cubemap_model2"
TOOL.ClientConVar[ "index" ] = 0

TOOL.Information = {
	{name = "left0", stage = 0, icon = "gui/lmb.png"},
	{name = "right0", stage = 0, icon = "gui/rmb.png"},
	{name = "reload0", stage = 0, icon = "gui/r.png"},
}

if CLIENT then
	language.Add("tool.submatpainter.name", "Color - Submaterial TF2 Painter")
	language.Add("tool.submatpainter.desc", "Paint TF2 items with any color, as long as they're paintable. Supports painting multiple materials")

	language.Add("tool.submatpainter.left0", "Add paint color")
	language.Add("tool.submatpainter.right0", "Copy paint color")
	language.Add("tool.submatpainter.reload0", "Remove paint color")

	language.Add("Undone_submatpainter", "Undone TF2 Item Paint")
	language.Add( "Undone_ReplacementEffect", "Undone Effect (replacement)" )
end




function TOOL:LeftClick(trace)

	local r = self:GetClientNumber("r", 0)
	local g = self:GetClientNumber("g", 0)
	local b = self:GetClientNumber("b", 0)
	local compatibility =	self:GetClientNumber( "effectcompatibility", 0 )

	//0,0,0 (and anything darker than 3,3,3) is used by the vmt to mean "no paint color", 
	//so if the player is trying to paint something pure black, then redirect it to a color that actually works
	if r < 3 and g < 3 and b < 3 then
		r = 3
		g = 3
		b = 3
	end

	local ply = self:GetOwner()

	if IsValid(trace.Entity) then
	
	local mat = self:GetClientInfo( "override" )
	local index = self:GetClientNumber( "index" , 0)


		-- Colors entire item TODO; seperate into materials
		if SERVER then

			GiveMatproxyTF2ItemPaint(ply, trace.Entity, {
				ColorR = r,
				ColorG = g,
				ColorB = b,
			})

		end
		
		if compatibility == 1 and trace.Entity:GetClass() == "prop_effect" then
			local compeffect = ents.Create( "prop_replacementeffect" )
			compeffect:SetModel(trace.Entity.AttachedEntity:GetModel())
			compeffect:SetSkin(trace.Entity:GetSkin())
			compeffect:SetPos(trace.Entity.AttachedEntity:GetPos())
			compeffect:SetAngles(trace.Entity.AttachedEntity:GetAngles())
			compeffect:SetPlayer(ply)
			trace.Entity:Remove()
			compeffect:Spawn()
			compeffect:Activate()

			GiveMatproxyTF2ItemPaint( ply, compeffect, { ColorR = r, ColorG = g, ColorB = b } )

			undo.Create("ReplacementEffect")
				undo.AddEntity( compeffect )
				undo.SetPlayer( ply )
			undo.Finish()

			return true
		end

		return true

	end

end




function TOOL:RightClick(trace)

	if IsValid(trace.Entity) then

		if SERVER then

			if IsValid(trace.Entity.AttachedEntity) then
				trace.Entity = trace.Entity.AttachedEntity
			end

			if trace.Entity.EntityMods and trace.Entity.EntityMods.MatproxyTF2ItemPaint then
				self:GetOwner():ConCommand("submatpainter_r " .. trace.Entity.EntityMods.MatproxyTF2ItemPaint.ColorR)
				self:GetOwner():ConCommand("submatpainter_g " .. trace.Entity.EntityMods.MatproxyTF2ItemPaint.ColorG)
				self:GetOwner():ConCommand("submatpainter_b " .. trace.Entity.EntityMods.MatproxyTF2ItemPaint.ColorB)
			end

		end

		return true

	end

end




function TOOL:Reload(trace)

	if IsValid(trace.Entity) then

		if SERVER then

			if IsValid(trace.Entity.AttachedEntity) then
				trace.Entity = trace.Entity.AttachedEntity
			end

			if IsValid(trace.Entity.ProxyentPaintColor) then
				trace.Entity.ProxyentPaintColor:Remove()
				trace.Entity.ProxyentPaintColor = nil
				duplicator.ClearEntityModifier(trace.Entity, "MatproxyTF2ItemPaint")
			end

		end

		return true

	end

end




if SERVER then

	function GiveMatproxyTF2ItemPaint(ply, ent, Data)

		if !IsValid(ent) then return end

		if IsValid(ent.AttachedEntity) then
			ent = ent.AttachedEntity
		end

		if IsValid(ent.ProxyentPaintColor) and ent.ProxyentPaintColor:GetTargetEnt() == ent then //NOTE: Entities pasted using GenericDuplicatorFunction (i.e. anything without custom dupe functionality) will still have the original entity's Proxyent value saved into their table because GenericDuplicatorFunction uses table.Merge(). In most cases this won't matter because the saved Proxyent is NULL, but if the original entity still exists, then the value will point to THAT entity's Proxyent instead, which we don't want to delete by mistake.
			ent.ProxyentPaintColor:Remove()
		end
		ent.ProxyentPaintColor = ents.Create("proxyent_tf2itempaint")

		ent.ProxyentPaintColor:SetTargetEnt(ent)
		ent.ProxyentPaintColor:SetPaintVector(Vector(Data.ColorR/255, Data.ColorG/255, Data.ColorB/255))

		ent.ProxyentPaintColor:Spawn()
		ent.ProxyentPaintColor:Activate()

		duplicator.StoreEntityModifier(ent, "MatproxyTF2ItemPaint", Data)

	end
	

	duplicator.RegisterEntityModifier("MatproxyTF2ItemPaint", GiveMatproxyTF2ItemPaint)

end




local ConVarsDefault = TOOL:BuildConVarList()
ConVarsDefault["submatpainter_effectcompatibility"] = nil  //don't save the compatibility mode in presets, it doesn't have anything to do with anything
ConVarsDefault["submatpainter_whichlist"] = nil  //don't save the selected list in presets


----- Damn Dirty fix... Thx for Wire Advanced tool developer
	
local function get_active_tool(ply, tool)
	-- find toolgun
	local activeWep = ply:GetActiveWeapon()
	if not IsValid(activeWep) or activeWep:GetClass() ~= "gmod_tool" or activeWep.Mode ~= tool then return end

	return activeWep:GetToolObject(tool)
end

if game.SinglePlayer() then -- wtfgarry (these functions don't get called clientside in single player so we need this hack to fix it)
	if SERVER then
		util.AddNetworkString( "submatpainter_wtfgarry" )
		local function send( ply, funcname )
			net.Start( "submatpainter_wtfgarry" )
				net.WriteString( funcname )
			net.Send( ply )
		end
		
		--function TOOL:LeftClick() send( self:GetOwner(), "LeftClick" ) end
		--function TOOL:RightClick() send( self:GetOwner(), "RightClick" ) end
		--function TOOL:Reload() send( self:GetOwner(), "Reload" ) end -- commented out because it b
	elseif CLIENT then
		net.Receive( "submatpainter_wtfgarry", function( len )
			local funcname = net.ReadString()
			local tool = get_active_tool( LocalPlayer(), "submatpainter" )
			if not tool then return end
			tool[funcname]( tool, LocalPlayer():GetEyeTrace() )
		end)
	end
end

--------------------------------------------------

if CLIENT then


	TOOL.AimEnt = nil
	TOOL.HudData = {}
	TOOL.SelIndx = 1
	TOOL.ToolMatString = ""

	function TOOL:Scroll(trace,dir)
		if !IsValid(self.AimEnt) then return end
		local Mats=self.AimEnt:GetMaterials()
		local MatCount=table.Count(Mats)
		self.SelIndx = self.SelIndx + dir
		if(self.SelIndx<0) then self.SelIndx = MatCount end
		if(self.SelIndx>MatCount) then self.SelIndx = 0 end
		RunConsoleCommand("submatpainter_index",tostring(self.SelIndx))
		return true
		--self.HudData.EntCurMat=Material(self.AimEnt:GetMaterials()[self.SelIndx])
		
	end
	function TOOL:ScrollUp(trace) return self:Scroll(trace,-1) end
	function TOOL:ScrollDown(trace) return self:Scroll(trace,1) end


	
---- Thx wire_adv dev again...
	local function hookfunc( ply, bind, pressed )
		if not pressed then return end
		if bind == "invnext" then
			local self = get_active_tool(ply, "submatpainter")
			if not self then return end
			
			return self:ScrollDown(ply:GetEyeTraceNoCursor())
		elseif bind == "invprev" then
			local self = get_active_tool(ply, "submatpainter")
			if not self then return end

			return self:ScrollUp(ply:GetEyeTraceNoCursor())
		end
	end
	
	if game.SinglePlayer() then -- wtfgarry (have to have a delay in single player or the hook won't get added)
		timer.Simple(5,function() hook.Add( "PlayerBindPress", "submat_tool_playerbindpress", hookfunc ) end)
	else
		hook.Add( "PlayerBindPress", "submat_tool_playerbindpress", hookfunc )
	end
--------------------------------------------------


	local function FixVertexLitMaterial(Mat)
		
		--
		-- If it's a vertexlitgeneric material we need to change it to be
		-- UnlitGeneric so it doesn't go dark when we enter a dark room
		-- and flicker all about
		--
		if not Mat then return Mat end
		local strImage = Mat:GetName()
		
		if ( string.find( Mat:GetShader(), "VertexLitGeneric" ) || string.find( Mat:GetShader(), "Cable" ) ) then
		
			local t = Mat:GetString( "$basetexture" )
			
			if ( t ) then
			
				local params = {}
				params[ "$basetexture" ] = t
				params[ "$vertexcolor" ] = 1
				params[ "$vertexalpha" ] = 1
				
				Mat = CreateMaterial( strImage .. "_hud_fx", "UnlitGeneric", params )
			
			end
			
		end
		
		return Mat
		
	end	

	function TOOL:Think( )
		local ent=LocalPlayer():GetEyeTraceNoCursor().Entity
		if ( IsValid( ent.AttachedEntity ) ) then ent = ent.AttachedEntity end
		if self.AimEnt ~= ent then
			
			self.AimEnt=ent
			if IsValid(self.AimEnt) then
				self.SelIndx=0
				RunConsoleCommand("submatpainter_index",tostring(self.SelIndx))
				self.HudData.Mats=self.AimEnt:GetMaterials()

			end
			--print("ThinkUpdate "..tostring(self.AimEnt))
		end

			if IsValid(self.AimEnt) then
				self.HudData.CurMats=table.Copy(self.HudData.Mats)
				self.HudData.OvrMats={}
	
				local MatCount=table.Count(self.HudData.Mats)
				for i=1,MatCount do
					local mat=self.AimEnt:GetSubMaterial(i-1)
					if mat and mat ~= "" then self.HudData.OvrMats[i]=mat end	
				end
				table.Merge(self.HudData.CurMats,self.HudData.OvrMats)
				self.HudData.GlobalMat=self.AimEnt:GetMaterial()
				local EntCurMatString=self.HudData.GlobalMat
				local EntOrigMatString=self.HudData.GlobalMat
				if self.SelIndx > 0 then EntCurMatString=self.HudData.CurMats[self.SelIndx]; EntOrigMatString=self.HudData.Mats[self.SelIndx] end 
				if self.HudData.EntCurMatString~=EntCurMatString then
					self.HudData.EntCurMatString=EntCurMatString
					self.HudData.EntCurMat=FixVertexLitMaterial(Material(EntCurMatString)) 
				end
				if self.HudData.EntOrigMatString~=EntOrigMatString then
					self.HudData.EntOrigMatString=EntOrigMatString
					self.HudData.EntOrigMat=FixVertexLitMaterial(Material(EntOrigMatString)) 
				end
			end
		
		if IsValid(self.AimEnt) and self.ToolMatString~=GetConVarString("submatpainter_override") then
			self.ToolMatString=GetConVarString("submatpainter_override")
 			self.HudData.ToolMat=FixVertexLitMaterial(Material(self.ToolMatString))
		end
		
	end
	function TOOL:DrawHUD( )
		if IsValid(self.AimEnt) then

			---- List
			local Rg=ScrW()/2-50
			local MaxW = 0
			local TextH = 0
			surface.SetFont("ChatFont")
			local Hdr=tostring(self.AimEnt)..": "..tostring(table.Count(self.HudData.Mats)).." materials"
			MaxW,TextH=surface.GetTextSize(Hdr)
			local HdrH = TextH+5
			for _,s in pairs(self.HudData.CurMats) do
				local ts,_=surface.GetTextSize(s)
				if MaxW<ts then MaxW=ts end
			end
			local LH=4*2+HdrH+TextH*(1+table.Count(self.HudData.Mats))
			local LW=4*2+MaxW
			local LL=Rg-LW
			local LT=ScrH()/2-LH/2
			surface.SetDrawColor(Color(64,64,95,191))
			--surface.SetMaterial(self.HudData.EntCurMat)
			surface.DrawRect(LL, LT, LW, LH)
			surface.SetTextColor(Color(255,255,255,255))
			surface.SetTextPos(LL+4,LT+4)
			surface.DrawText(Hdr)
			surface.SetDrawColor(Color(255,255,255,255))
			surface.DrawLine(LL+3,LT+4+TextH+3,Rg-3,LT+4+TextH+3)

			surface.SetDrawColor(Color(0,127,0,191))
			surface.DrawRect(LL+3, LT+4+HdrH+TextH*self.SelIndx, LW-3-3, TextH)
			
			local s="<none>"
			if not self.HudData.GlobalMat or self.HudData.GlobalMat == "" then 
				surface.SetTextColor(Color(255,255,255,255)) 
			else surface.SetTextColor(Color(0,0,255,255)); s=self.HudData.GlobalMat end
		
			surface.SetTextPos(LL+4,LT+4+HdrH)
			surface.DrawText(s)



			for i,s in pairs(self.HudData.CurMats) do
				if self.HudData.OvrMats[i] then surface.SetTextColor(Color(255,0,0,255)) else surface.SetTextColor(Color(255,255,255,255)) end
				surface.SetTextPos(LL+4,LT+4+HdrH+TextH*i)
				surface.DrawText(s)
			end
			---- Info box
			
			
			--local MaxW = 0
			local StrToolInfo = "Tool color:"
			local StrOrigMatInfo = "Model original color:"
			local StrCurMatInfo = "Model current color:"
			local MaxW,_=surface.GetTextSize(StrToolInfo)
			local ts,_=surface.GetTextSize(StrOrigMatInfo)
			if MaxW<ts then MaxW=ts end
			local ts,_=surface.GetTextSize(StrCurMatInfo)
			if MaxW<ts then MaxW=ts end
			local ts,_=surface.GetTextSize(self.ToolMatString)
			if MaxW<ts then MaxW=ts end
			local ts,_=surface.GetTextSize(self.HudData.EntOrigMatString)
			if MaxW<ts then MaxW=ts end
			local ts,_=surface.GetTextSize(self.HudData.EntCurMatString)
			if MaxW<ts then MaxW=ts end
		
			local IL=ScrW()/2+50
			local IH=4*4+(64)*3
			local IT=ScrH()/2-IH/2
			surface.SetDrawColor(Color(64,64,95,191))
			surface.DrawRect(IL, IT, 76+MaxW, IH)    -- 4+64+4+MaxW+4

			surface.SetTextColor(Color(255,255,255,255))

			surface.SetDrawColor(Color(255,255,255,255))
			if self.HudData.ToolMat  then
				surface.SetMaterial(self.HudData.ToolMat)
				surface.DrawTexturedRect(IL+4, IT+4, 64, 64)
			end
			surface.SetTextPos(IL+4+64+4,IT+8)
			surface.DrawText(StrToolInfo)
			surface.SetTextPos(IL+4+64+4,IT+8+TextH)
			surface.DrawText(self.ToolMatString)
			surface.SetTextPos(IL+4+64+4,IT+8+TextH*2)
			surface.DrawText(self.SelIndx==0 and "[Global]" or "Index: "..self.SelIndx-1)
	
						

			if self.HudData.EntOrigMat  then
				surface.SetMaterial(self.HudData.EntOrigMat)	
				surface.DrawTexturedRect(IL+4, IT+4+(64+4), 64, 64)
			end
			surface.SetTextPos(IL+4+64+4,IT+8+64+4)
			surface.DrawText(StrOrigMatInfo)
			surface.SetTextPos(IL+4+64+4,IT+8+64+4+TextH)
			surface.DrawText(self.HudData.EntOrigMatString)

			if self.HudData.EntCurMat  then
				surface.SetMaterial(self.HudData.EntCurMat)
				surface.DrawTexturedRect(IL+4, IT+4+(64+4)*2, 64, 64)
			end
			surface.SetTextPos(IL+4+64+4,IT+8+(64+4)*2)
			surface.DrawText(StrCurMatInfo)
			surface.SetTextPos(IL+4+64+4,IT+8+(64+4)*2+TextH)
			surface.DrawText(self.HudData.EntCurMatString)


--			surface.SetMaterial(nil)
			

			--draw.RoundedBox( 2, ScrW()/2-50, ScrH()/2-50, 100, 100, Color(255,255,255,255) ) 
			
		--	print("DrawHUD "..tostring(self.AimEnt))
		end
	end


end

function TOOL.BuildCPanel(panel)

	panel:AddControl("Header", {
		Text = "Color - Submaterial TF2 Painter",
		Description = "Paint TF2 items with any color, as long as they're paintable. Supports painting multiple materials"
	})

	//Presets
	panel:AddControl("ComboBox", {
		MenuButton = 1,
		Folder = "submatpainter",
		//Options = {
		//	["#preset.default"] = ConVarsDefault
		//},
		CVars = table.GetKeys(ConVarsDefault)
	})

	panel:AddControl("ListBox", {
		Label = "Category",
		Height = 85,
		Options = {
			["1: Original Paints"] = {submatpainter_whichlist = "original"},
			["2: The Bad-Paintening of December 2010"] = {submatpainter_whichlist = "theonewithpinkandlime"},
			["3: Newer Paints"] = {submatpainter_whichlist = "newpaints"},
			["4: Team Colors"] = {submatpainter_whichlist = "teampaints"},
		},
	})

	panel.PaintList = panel:AddControl("ListBox", {
		Label = "Color",
		Height = 153,
		Options = {},
	})

	panel.PaintList.OldThink = panel.PaintList.Think
	panel.PaintList.Think = function(self, ...)
		local whichlist = GetConVar("submatpainter_whichlist"):GetString()
		if whichlist != self.CurWhichlist then
			self.CurWhichlist = whichlist
			local data = {}

			//Original Paints
			if whichlist == "original" then
				data["A Deep Commitment to Purple"] = {
					submatpainter_r = "125",
					submatpainter_g = "64",
					submatpainter_b = "113",
				}
				data["Aged Moustache Gray"] = {
					submatpainter_r = "126",
					submatpainter_g = "126",
					submatpainter_b = "126",
				}
				data["Australium Gold"] = {
					submatpainter_r = "231",
					submatpainter_g = "181",
					submatpainter_b = "59",
				}
				data["Color 216-190-216"] = {
					submatpainter_r = "216",
					submatpainter_g = "190",
					submatpainter_b = "216",
				}
				data["Indubitably Green"] = {
					submatpainter_r = "114",
					submatpainter_g = "158",
					submatpainter_b = "66",
				}
				data["Mann Co. Orange"] = {
					submatpainter_r = "207",
					submatpainter_g = "115",
					submatpainter_b = "54",
				}
				data["Muskelmannbraun"] = {
					submatpainter_r = "165",
					submatpainter_g = "117",
					submatpainter_b = "69",
				}
				data["Noble Hatter's Violet"] = {
					submatpainter_r = "81",
					submatpainter_g = "56",
					submatpainter_b = "74",
				}
				data["Particularly Drab Tincture"] = {
					submatpainter_r = "197",
					submatpainter_g = "175",
					submatpainter_b = "145",
				}
				data["Radigan Conagher Brown"] = {
					submatpainter_r = "105",
					submatpainter_g = "77",
					submatpainter_b = "58",
				}
				data["Ye Olde Rustic Colour"] = {
					submatpainter_r = "124",
					submatpainter_g = "108",
					submatpainter_b = "87",
				}
				data["Zepheniah's Greed"] = {
					submatpainter_r = "66",
					submatpainter_g = "79",
					submatpainter_b = "59",
				}
				data["An Extraordinary Abundance of Tinge"] = {
					submatpainter_r = "230",
					submatpainter_g = "230",
					submatpainter_b = "230",
				}
				data["A Distinctive Lack of Hue"] = {
					submatpainter_r = "20",
					submatpainter_g = "20",
					submatpainter_b = "20",
				}
			end

			//The Bad-Paintening of December 2010
			if whichlist == "theonewithpinkandlime" then
				data["A Color Similar to Slate"] = {
					submatpainter_r = "47",
					submatpainter_g = "79",
					submatpainter_b = "79",
				}
				data["Dark Salmon Injustice"] = {
					submatpainter_r = "233",
					submatpainter_g = "150",
					submatpainter_b = "122",
				}
				data["Drably Olive"] = {
					submatpainter_r = "128",
					submatpainter_g = "128",
					submatpainter_b = "0",
				}
				data["The Color of a Gentlemann's Business Pants"] = {
					submatpainter_r = "240",
					submatpainter_g = "230",
					submatpainter_b = "140",
				}
				data["The Bitter Taste of Defeat and Lime"] = {
					submatpainter_r = "50",
					submatpainter_g = "205",
					submatpainter_b = "50",
				}
				data["Pink as Hell"] = {
					submatpainter_r = "255",
					submatpainter_g = "105",
					submatpainter_b = "180",
				}
			end

			//Newer Paints
			if whichlist == "newpaints" then
				data["A Mann's Mint"] = {
					submatpainter_r = "188",
					submatpainter_g = "221",
					submatpainter_b = "179",
				}
				data["After Eight"] = {
					submatpainter_r = "45",
					submatpainter_g = "45",
					submatpainter_b = "36",
				}
			end

			//Team Colors
			if whichlist == "teampaints" then
				data["Team Spirit, RED"] = {
					submatpainter_r = "184",
					submatpainter_g = "56",
					submatpainter_b = "59",
				}
				data["Team Spirit, BLU"] = {
					submatpainter_r = "88",
					submatpainter_g = "133",
					submatpainter_b = "162",
				}
				data["The Value of Teamwork, RED"] = {
					submatpainter_r = "128",
					submatpainter_g = "48",
					submatpainter_b = "32",
				}
				data["The Value of Teamwork, BLU"] = {
					submatpainter_r = "37",
					submatpainter_g = "109",
					submatpainter_b = "141",
				}
				data["Waterlogged Lab Coat, RED"] = {
					submatpainter_r = "168",
					submatpainter_g = "154",
					submatpainter_b = "140",
				}
				data["Waterlogged Lab Coat, BLU"] = {
					submatpainter_r = "131",
					submatpainter_g = "159",
					submatpainter_b = "163",
				}
				data["An Air of Debonair, RED"] = {
					submatpainter_r = "101",
					submatpainter_g = "71",
					submatpainter_b = "64",
				}
				data["An Air of Debonair, BLU"] = {
					submatpainter_r = "40",
					submatpainter_g = "57",
					submatpainter_b = "77",
				}
				data["Balaclavas Are Forever, RED"] = {
					submatpainter_r = "59",
					submatpainter_g = "31",
					submatpainter_b = "35",
				}
				data["Balaclavas Are Forever, BLU"] = {
					submatpainter_r = "24",
					submatpainter_g = "35",
					submatpainter_b = "61",
				}
				data["Operator's Overalls, RED"] = {
					submatpainter_r = "72",
					submatpainter_g = "56",
					submatpainter_b = "56",
				}
				data["Operator's Overalls, BLU"] = {
					submatpainter_r = "56",
					submatpainter_g = "66",
					submatpainter_b = "72",
				}
				data["Cream Spirit, RED"] = {
					submatpainter_r = "195",
					submatpainter_g = "108",
					submatpainter_b = "45",
				}
				data["Cream Spirit, BLU"] = {
					submatpainter_r = "184",
					submatpainter_g = "128",
					submatpainter_b = "53",
				}
			end


			//Replace the options currently in the panel with the ones in the data table
			panel.PaintList.Options = {}
			for name, command in pairs(data) do
				panel.PaintList.Options[name] = command
			end

			panel.PaintList:Clear()
			for k, v in pairs(panel.PaintList.Options) do
				local line = panel.PaintList:AddLine(k)
				line.data = v
			end
			panel.PaintList:SortByColumn(1, false)
		end
		if panel.PaintList.OldThink then
			return panel.PaintList.OldThink(self, ...)
		end
	end

	panel:AddControl("Color", {
		Label = "Custom Color",
		Red = "submatpainter_r",
		Blue = "submatpainter_b",
		Green = "submatpainter_g",
		ShowHSV = 1,
		ShowRGB = 1,
		Multiplier = 255,
	})
	
	panel:AddControl( "CheckBox", {Label = "Bone Merger Compatibility Mode", Command = "submatpainter_effectcompatibility"})
	panel:ControlHelp( "If selected, painting replaces effects (props without physics) with a different kind of entity that doesn't lose its color when bone merged." )

end