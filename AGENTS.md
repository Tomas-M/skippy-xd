# Fork workflow

Maintain changes in Tomas-M/skippy-xd. Do not submit further pull requests to the official felixfung/skippy-xd repository. Slax builds the current fork HEAD without version, commit or archive hash pins.

Labels use PangoXft. Font configuration uses Pango descriptions with ordered, comma-separated family names and a point size. Keep shaped layouts cached across redraws and use Pango for UTF-8 ellipsization.

Top and bottom application captions belong to the thumbnail frame. Include their fixed screen-pixel height in every layout and keep source image transforms independent of the caption surface. Middle-position application labels retain their floating placement. Workspace labels overlay the inside of each desktop preview and use [desktop-label] padding, margin and radius in screen pixels. Keep their Pango layouts and rounded masks cached. [label] padding controls extra space on all four sides of application captions, including their icon; the default is 2 screen pixels.

Paging must remain open on empty desktops when application, launcher or hidden support windows are destroyed. Only expose/switch modes may close when their last application disappears.

With desktop cycling disabled, Alt+Tab on an empty application list must complete its IPC request without ever mapping the switcher window. Empty paging must still be available.
