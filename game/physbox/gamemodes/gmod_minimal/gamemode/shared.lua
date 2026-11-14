GM.Name    = "Minimal"
GM.Author  = ""
GM.Email   = ""
GM.Website = ""

DeriveGamemode("sandbox")

DEFINE_BASECLASS( "gamemode_base" )

function GM:PlayerSpawnSENT(ply, class)
    return true
end