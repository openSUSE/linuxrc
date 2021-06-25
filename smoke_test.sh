#! /bin/sh

# exit on error immediately
set -e

function start_command()
{
  echo "Starting command '$2'..."
  # run the command in a new tmux session (-d = detach, -s = session name)
  # force 80x25 terminal size
  TERM=screen tmux new-session -d -s "$1" -x 80 -y 25 $2
}

function dump_screen()
{
  echo "----------------------- Screen Dump Begin -----------------------------"
  if [ "$TRAVIS" == "1" ]; then
    # the sed call transforms spaces to non-breakable UTF-8 spaces because
    # Travis does not display a normal space sequence correctly
    tmux capture-pane -e -p -t "$1" | sed 's/ /\xC2\xA0/g'
  else
    tmux capture-pane -e -p -t "$1"
  fi
  # reset the terminal to restore the initial color settings
  # (might be changed by the color dump)
  tput init
  echo "----------------------- Screen Dump End -------------------------------"
}

function expect_text()
{
  if tmux capture-pane -p -t "$1" | grep -q "$2"; then
    echo "Matched expected text: '$2'"
  else
    echo "ERROR: No match for expected text '$2'"
    exit 1
  fi
}

function not_expect_text()
{
  if tmux capture-pane -p -t "$1" | grep -q "$2"; then
    echo "ERROR: Matched unexpected text: '$2'"
    exit 1
  fi
}

function send_keys()
{
  echo "Sending keys: $2"
  tmux send-keys -t "$1" "$2"
}

function process_exited()
{
  if tmux has-session "$1" 2> /dev/null; then
    echo "ERROR: The process is still running!"
    exit 1
  else
    echo "The process exited, OK"
  fi
}

# name of the tmux session
SESSION=linuxrc

###############################################################################

# run linuxrc in line mode in a new session
start_command $SESSION "./linuxrc linemode=1"

# wait a bit to ensure it is up
# TODO: wait until the screen contains the expected text (with a timeout),
# 5 seconds might not be enough on a slow or overloaded machine
sleep 5
dump_screen $SESSION

expect_text $SESSION "Please make sure your installation medium is available"

# quit via hidden extra menu
send_keys $SESSION "x"
send_keys $SESSION "Enter"

sleep 3
dump_screen $SESSION
expect_text $SESSION "Linuxrc extras"
expect_text $SESSION "5) Quit linuxrc"
send_keys $SESSION "5"
send_keys $SESSION "Enter"

sleep 3
process_exited $SESSION

###############################################################################

# run linuxrc in standard mode in a new session
start_command $SESSION ./linuxrc

# wait a bit to ensure it is up
# TODO: wait until the screen contains the expected text (with a timeout),
# 5 seconds might not be enough on a slow or overloaded machine
sleep 5
dump_screen $SESSION
expect_text $SESSION "Please make sure your installation medium is available"

# FIXME: ??? sending Ctrl+C for some reason does not work in Travis/GitHub Actions,
# so kill the process and finish here
if [ "$TRAVIS" == "1" -o "$GITHUB_RUN_ID" != "" ]; then
  echo "CI environment set, stopping the test..."
  kill -9 `pidof linuxrc`
  exit 0
fi

# quit via Ctrl+C...
send_keys $SESSION "C-c"

sleep 3
dump_screen $SESSION
expect_text $SESSION "You cannot exit Linuxrc"
# then use hidden q
send_keys $SESSION "q"

sleep 3
process_exited $SESSION

# TODO: trap the signals and do a cleanup at the end
# (kill the process if it is still running, use tmux kill-session ?)
