package require tkpath 0.3.3

set t .c_gradients
toplevel $t
set w $t.c
pack [tkp::canvas $w -bg white -width 480 -height 400]

set rainbow [::tkp::gradientstopsstyle rainbow]

set g1 [$w gradient create linear -stops {{0 lightblue} {1 blue}}]
$w create prect 10 10 210 60 -fill $g1
$w create text 220 20 -anchor w -text "-stops {{0 lightblue} {1 blue}}"

set g2 [$w gradient create linear -stops {{0 "#f60"} {1 "#ff6"}} \
  -lineartransition {50 0 160 0} -units userspace]
$w create prect 10 70 210 120 -fill $g2
$w create text 220 80 -anchor w -text "-stops {{0 #f60} {1 #ff6}}"
$w create text 220 100 -anchor w -text "-lineartransition {50 0 160 0} -units userspace"

set g5 [$w gradient create linear -stops {{0 lightgreen} {1 green}}]
$w create prect 10 130 210 180 -fill $g5
$w create text 220 140 -anchor w -text "-stops {{0 lightgreen} {1 green}}"

set g3 [$w gradient create linear -stops {{0 "#f60"} {1 "#ff6"}} \
  -lineartransition {0 0 0 1}]
$w create path "M 40 200 q 60 -200 120 0 z" -fill $g3 -fillopacity 0.8

set g4 [$w gradient create linear -stops $rainbow]
$w create prect 10 210 210 260 -fill $g4
$w create text 220 220 -anchor w -text "rainbow"

set g6 [$w gradient create radial -stops {{0 white} {1 black}}]
$w create circle 60 330 -r 50 -fill $g6

set g7 [$w gradient create radial -stops {{0 white} {1 black}}  \
  -radialtransition {0.6 0.4 0.5 0.7 0.3}]
$w create circle 200 330 -r 50 -fill $g7 -stroke ""

set g8 [$w gradient create radial -stops {{0 white} {1 black}}  \
  -radialtransition {0.6 0.4 0.8 0.7 0.3}]
$w create circle 340 330 -r 50 -fill $g8 -stroke ""


proc GradientsOnButton {w} {
    set id [$w find withtag current]
    if {$id ne ""} {
	set type [$w type $id]
	switch -- $type {
	    prect - path - circle - ellipse {
		set stroke [$w itemcget $id -stroke]
		set fill [$w itemcget $id -fill]
		puts "Hit a $type with stroke $stroke and fill $fill"
	    }
	}
    }
}
$w bind all <Button-1> [list GradientsOnButton $w]
