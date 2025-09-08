// Created by Crowbar 0.42.0.0

$ModelName "Weapons/v_Physics.mdl"

$BodyGroup "studio"
{
	studio "v_physics_reference.smd"
}


$SurfaceProp "default"

$Contents "solid"

$MaxEyeDeflection 90

$CDMaterials "Models\Weapons\v_physics_gun\\"
$CDMaterials "models\Weapons\V_hand\\"

$Attachment "0" "muzzle" 0 12 0 rotate 0 0 0

$CBox 0 0 0 0 0 0

$BBox 22.314 -23.358 -21.609 75.564 1.327 -1.063

// $HBoxSet "default"
// $HBox 0 "Bip01 L Forearm" 0 -3.035958 -2.879772 15.996998 3.010704 2.457477 ""
// $HBox 0 "Bip01 L Hand" 0 -1.78318 -2.909699 4.806381 17.776594 2.447161 ""
// $HBox 0 "Bip01 L Finger0" 0 -0.71799 -1.63253 1.506958 0.934983 0.830752 ""
// $HBox 0 "Bip01 L Finger01" -0.876646 -0.687521 -0.664787 1.03474 0.751215 0.695046 ""
// $HBox 0 "Bip01 L Finger1" 0 -0.821535 -0.625329 2.124263 0.770541 0.706037 ""
// $HBox 0 "Bip01 L Finger2" 0 -0.925969 -0.515055 2.325402 0.630242 0.837631 ""
// $HBox 0 "Bip01 L Finger31" 0 -0.677995 -0.611816 1.627932 0.358374 0.529782 ""
// $HBox 0 "Bip01 L Finger4" 0 -0.882738 -0.560181 1.697775 0.402007 0.611231 ""
// $HBox 0 "Dummy15" -7.15804 -8.419433 -14.498471 14.290165 10.655806 32.095074 ""
// $HBox 0 "Dummy14" -5.643439 -5.624474 -9.122003 5.630161 5.649122 18.862991 ""


$Animation "a_idle1" "v_physics_anims\a_idle1.smd" {
	fps 30
	loop
}

$Animation "a_draw" "v_physics_anims\a_draw.smd" {
	fps 30
}

$Animation "a_fire" "v_physics_anims\a_fire.smd" {
	fps 30
	loop
}

$Animation "a_fire2" "v_physics_anims\a_fire2.smd" {
	fps 30
	loop
}

$Animation "a_winddown" "v_physics_anims\a_winddown.smd" {
	fps 30
}

$Sequence "idle1" {
	"a_idle1"
	activity "ACT_VM_IDLE" 1
	fadein 0.2
	fadeout 0.2
	fps 30
	loop
}

$Sequence "draw" {
	"a_draw"
	activity "ACT_VM_DRAW" 1
	fadein 0.2
	fadeout 0.2
	snap
	fps 30
}

$Sequence "fire" {
	"a_fire"
	activity "ACT_VM_PRIMARYATTACK" 1
	fadein 0.2
	fadeout 0.2
	fps 30
	loop
}

$Sequence "fire2" {
	"a_fire2"
	activity "ACT_VM_SECONDARYATTACK" 1
	fadein 0.2
	fadeout 0.2
	fps 30
	loop
}

$Sequence "winddown" {
	"a_winddown"
	fadein 0.2
	fadeout 0.2
	fps 30
}
