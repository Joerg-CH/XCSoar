/*
Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000-2013 The XCSoar Project
  A detailed list of copyright holders can be found in the file "AUTHORS".

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
}
*/

#include "Input.hpp"
#include "MergeMouse.hpp"
#include "Event/Shared/Event.hpp"
#include "Event/Queue.hpp"
#include "IO/Async/IOLoop.hpp"
#include "Asset.hpp"

#include <linux/input.h>

#ifdef KEY_DOWN
#undef KEY_DOWN
#endif
#ifdef KEY_UP
#undef KEY_UP
#endif

gcc_const
static unsigned
TranslateKeyCode(unsigned key_code)
{
  if (IsKobo()) {
    switch (key_code) {
    case KEY_HOME:
      /* the Kobo Touch "home" button shall open the menu */
      return KEY_MENU;
    }
  }

  return key_code;
}

bool
LinuxInputDevice::Open(const char *path)
{
  if (!fd.OpenReadOnly(path))
    return false;

  fd.SetNonBlocking();
  io_loop.Add(fd.Get(), io_loop.READ, *this);

  rel_x = rel_y = 0;
  down = false;
  moving = pressing = releasing = false;
  return true;
}

void
LinuxInputDevice::Close()
{
  if (!IsOpen())
    return;

  io_loop.Remove(fd.Get());
  fd.Close();
}

void
LinuxInputDevice::Read()
{
  struct input_event buffer[64];
  const ssize_t nbytes = fd.Read(buffer, sizeof(buffer));
  if (nbytes < 0) {
    /* device has failed or was unplugged - bail out */
    if (errno != EAGAIN && errno != EINTR)
      Close();
    return;
  }

  unsigned n = size_t(nbytes) / sizeof(buffer[0]);

  for (unsigned i = 0; i < n; ++i) {
    const struct input_event &e = buffer[i];

    switch (e.type) {
    case EV_SYN:
      if (e.code == SYN_REPORT) {
        /* commit the finger movement */

        const bool pressed = pressing;
        const bool released = releasing;
        pressing = releasing = false;

        if (pressed)
          merge.SetDown(true);

        if (released)
          merge.SetDown(false);

        if (IsKobo() && released) {
          /* workaround: on the Kobo Touch N905B, releasing the touch
             screen reliably produces a finger position that is way
             off; in that case, ignore finger movement */
          moving = false;
          edit_position = public_position;
        }

        if (moving) {
          moving = false;
          public_position = edit_position;
          merge.MoveAbsolute(public_position.x, public_position.y);
        } else if (rel_x != 0 || rel_y != 0) {
          merge.MoveRelative(rel_x, rel_y);
          rel_x = rel_y = 0;
        }
      }

      break;

    case EV_KEY:
      if (e.code == BTN_TOUCH || e.code == BTN_MOUSE) {
        bool new_down = e.value;
        if (new_down != down) {
          down = new_down;
          if (new_down)
            pressing = true;
          else
            releasing = true;
        }
      } else
        queue.Push(Event(e.value ? Event::KEY_DOWN : Event::KEY_UP,
                         TranslateKeyCode(e.code)));

      break;

    case EV_ABS:
      moving = true;

      switch (e.code) {
      case ABS_X:
        edit_position.x = e.value;
        break;

      case ABS_Y:
        edit_position.y = e.value;
        break;
      }

      break;

    case EV_REL:
      switch (e.code) {
      case REL_X:
        rel_x += e.value;
        break;

      case ABS_Y:
        rel_y += e.value;
        break;
      }

      break;
    }
  }
}

bool
LinuxInputDevice::OnFileEvent(int fd, unsigned mask)
{
  Read();

  return true;
}
