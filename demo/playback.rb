#coding: GBK

require './seal'
include Audio

Audio.startup

src = Source.new
src.buffer = Buffer.new("D:/music/���� ̫���� �̹���.mp3")
src.play
sleep(10)

Audio.cleanup