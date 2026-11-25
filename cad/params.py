"""Design parameters for the peristaltic condensate pump.

All dimensions in mm. This file mirrors docs/design.md: change a value here,
re-run `cad/build.sh`, and every part is regenerated.

Coordinate system of the pump head:
  z = 0  motor mounting face (= underside of the pump base plate), z up
  rotor axis = z axis
  the track is a "U" open towards -y; the hose legs leave through the -y wall
"""

# --------------------------------------------------------------------------
# Printing
# --------------------------------------------------------------------------
CLR = 0.2            # general sliding clearance per side
HOLE_M3 = 3.4        # M3 clearance hole
HOLE_M4 = 4.5        # M4 clearance hole
INSERT_M3_D = 4.0    # hole for M3 heat-set insert (check your insert's spec)
INSERT_M3_L = 6.0
INSERT_M4_D = 5.6    # hole for M4 heat-set insert
INSERT_M4_L = 5.0
NUT_M3_AF = 5.5      # M3 nut across flats
NUT_M3_T = 2.4

# --------------------------------------------------------------------------
# Hose (silicone 4 x 7)
# --------------------------------------------------------------------------
HOSE_ID = 4.0
HOSE_OD = 7.0
HOSE_WALL = (HOSE_OD - HOSE_ID) / 2

# --------------------------------------------------------------------------
# Rollers: 608 bearings, stacked 2 per roller
# (single 608 = 7 mm wide, a flattened 4x7 hose is ~9-10 mm wide)
# --------------------------------------------------------------------------
BRG_OD = 22.0
BRG_ID = 8.0
BRG_W = 7.0
BRG_INNER_RACE_D = 11.0      # spacer/boss diameter that touches only the inner race
ROLLER_STACK = 2
ROLLER_W = BRG_W * ROLLER_STACK
N_ROLLERS = 3
PITCH_R = 16.0               # roller axle radius (min practical for 3x Ø22)
ROLLER_R = PITCH_R + BRG_OD / 2          # 27.0 outer radius of rollers

# --------------------------------------------------------------------------
# Occlusion / track
# --------------------------------------------------------------------------
OCCLUSION_GAP = 2.6                      # nominal; 87 % of 2x wall
GAP_VARIANTS = (2.4, 2.6, 2.8)           # printed track inserts
TRACK_R = ROLLER_R + OCCLUSION_GAP       # 29.6 track surface radius
CAVITY_R = 34.0                          # housing cavity radius = insert outer radius
CAVITY_Y = 34.0                          # cavity extends to y = -CAVITY_Y (leg region)
HOSE_X = TRACK_R - HOSE_OD / 2           # hose centre-line x in the straight legs

# --------------------------------------------------------------------------
# Motor (measured 2026-11-25: 38x38x33, 31 mm hole pattern, Ø16.8x11 hub)
# --------------------------------------------------------------------------
MOTOR = "stock"                # "stock" (current 39 mm-class motor) or "nema17"
MOTOR_FRAME = 38.0
MOTOR_LEN = 33.0
MOTOR_HOLE_SPACING = 31.0      # also NEMA 17
MOTOR_PILOT_POCKET_D = 23.0    # clears the usual Ø22 pilot boss
MOTOR_PILOT_POCKET_H = 2.5
STOCK_HUB_D = 16.8
STOCK_HUB_H = 11.0
STOCK_HUB_Z0 = 1.0             # [TBC] gap between motor face and bottom of the hub
NEMA17_FRAME = 42.3
NEMA17_SHAFT_D = 5.0
NEMA17_SHAFT_FLAT = 4.5        # D-flat: distance from flat to opposite side
NEMA17_SHAFT_L = 22.0

# --------------------------------------------------------------------------
# Pump head housing
# --------------------------------------------------------------------------
PLATE_T = 5.0                  # base plate thickness (motor face to floor)
HOUSING_HALF = 43.0            # outer half-width (86 x 86 square)
HOUSING_CORNER_R = 6.0
LID_T = 4.0
LID_SCREW_XY = 38.5            # 4x M3 at (+-38.5, +-38.5)
EAR_W = 20.0                   # mounting ears on +-x
EAR_L = 12.0
EAR_HOLE = 4.5
HOSE_SADDLE_W = 6.6            # U-slot in the -y wall that holds the hose legs
HOSE_SQUEEZE = 0.4             # lid tab presses the hose this much (anti-creep)

# --------------------------------------------------------------------------
# Rotor
# --------------------------------------------------------------------------
DISC_R = ROLLER_R + 1.5        # discs are flanges that keep the hose centred
DISC_LOWER_T = 6.0
DISC_UPPER_T = 5.0             # holds M4 countersunk heads
CLAMP_OD = 30.0                # hub clamp below the lower disc
CLAMP_BORE = STOCK_HUB_D + 0.3
FLOOR_GAP = 0.8                # rotor clamp to floor
SPACER_T = 0.8                 # inner-race spacer (sleeve flange / disc boss)
AXLE_SLEEVE_OD = 7.95          # printed sleeve over the M4 bolt, inside the 608 bore
LID_GAP = 0.8                  # rotor top to lid underside
LID_BEARING = False            # optional 608 in the lid supporting the rotor top

# --------------------------------------------------------------------------
# Derived z stack (z = 0 at motor face)
# --------------------------------------------------------------------------
Z_FLOOR = PLATE_T
Z_HUB_TOP = STOCK_HUB_Z0 + STOCK_HUB_H                # 12.0
Z_CLAMP_BOT = Z_FLOOR + FLOOR_GAP                     # 5.8
Z_DISC1_BOT = Z_HUB_TOP + 0.3                         # 12.3
Z_DISC1_TOP = Z_DISC1_BOT + DISC_LOWER_T              # 18.3
Z_ROLLER_BOT = Z_DISC1_TOP + SPACER_T                 # 19.1
Z_ROLLER_TOP = Z_ROLLER_BOT + ROLLER_W                # 33.1
Z_DISC2_BOT = Z_ROLLER_TOP + SPACER_T                 # 33.9
Z_DISC2_TOP = Z_DISC2_BOT + DISC_UPPER_T              # 38.9
Z_HOSE = (Z_DISC1_TOP + Z_DISC2_BOT) / 2              # 26.1 hose centre height
Z_LID = Z_DISC2_TOP + LID_GAP                         # 39.7 lid underside = wall top
Z_TOP = Z_LID + LID_T

# --------------------------------------------------------------------------
# Reservoir (baseline: Masterflow square reservoir ~87 x 78 x 42)
# --------------------------------------------------------------------------
RES_X = 87.0                   # outer, along x
RES_Y = 78.0                   # outer, along y
RES_H = 40.0                   # body height (floor bottom to rim)
RES_WALL = 2.0                 # sensors read through this wall: keep it thin and flat
RES_FLOOR = 2.4
RES_CORNER_R = 5.0
RES_LID_T = 3.0
RES_LID_LIP_H = 0.0            # 0 = no lip (the 4 screws locate the lid; a lip would make it unprintable flat)
RES_SCREW_BOSS_D = 9.0         # inside corner bosses for 4x M3 lid screws
PICKUP_XY = (RES_X / 2 - 16.0, -(RES_Y / 2 - 16.0))  # pickup corner, nearest the pump
STRAINER_OD = 16.0             # printed strainer cage the hose end plugs into
STRAINER_H = 14.0
AC_INLET_D = 20.0              # AC drain hose drops in here (loose fit) [TBC Q14]
AC_INLET_COLLAR_H = 8.0
AC_INLET_XY = (-18.0, 8.0)
VENT_D = 3.0

# Level sensors (non-contact, from sensor_dimensions.png)
SENSOR_W = 13.2                # body width
SENSOR_W_EARS = 21.0
SENSOR_H = 24.1
SENSOR_T = 7.0
SENSOR_SENSE_FROM_BOTTOM = 12.0    # [TBC] assumed trip line at mid-body
SENSOR_L_TRIP_Z = 13.0             # above the INSIDE floor
SENSOR_H_TRIP_Z = 24.0             # 11 mm above L -> ~68 mL between sensors
SENSOR_CLIP_SLOT = 10.0            # +-5 mm height adjustment
SENSOR_CLIP_SCREW_DX = 16.5        # clip screws at +-16.5 mm beside the sensor centre
SENSOR_L_FACE = ("-y", -15.0)      # L sensor on the -y wall, centred at x = -15
SENSOR_H_FACE = ("+x", 12.0)       # H sensor on the +x wall, centred at y = +12
