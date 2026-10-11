ExtraStations.SA - station folders

Every station is a folder in here. The ready-made station is MyStation:

  MyStation\songs\    put your music here (.ogg .mp3 .wav .wma .wmv .aac .m4a)   needed
  MyStation\idents\   station ids, jingles                                         optional
  MyStation\ads\      adverts                                                      optional
  MyStation\talk\     DJ talk, news, traffic, weather                              optional

The optional folders may stay empty (or be deleted). A station without songs is not used.
Restart the game after adding files.

To add another station: copy the MyStation folder, give the copy a new name and add a section to
scripts\ExtraStations.SA.ini, for example

  [Station2]
  Name=My Second Station
  HostStation=14
  Folder=ExtraStations.SA\MySecondStation

The ini explains every setting.
