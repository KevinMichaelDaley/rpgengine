set confirm off
set pagination off
set print pretty on

# Run until crash
run 40090

# Print crash info
echo \n=== CRASH INFO ===\n
bt 10

echo \n=== ALL THREADS ===\n
thread apply all bt 3

echo \n=== FIBER DEBUG TRACE ===\n
# Try to print g_current_fiber on current thread
echo --- Current thread g_current_fiber ---\n
print g_current_fiber
if g_current_fiber != 0
  print g_current_fiber->id
  print g_current_fiber->swap_caller
  print g_current_fiber->swap_site
  print g_current_fiber->prev_fiber
  print g_current_fiber->last_worker
  print g_current_fiber->magic1
  print g_current_fiber->magic2
  print g_current_fiber->fn
  if g_current_fiber->prev_fiber != 0
    echo --- prev_fiber ---\n
    print g_current_fiber->prev_fiber->id
    print g_current_fiber->prev_fiber->swap_caller
    print g_current_fiber->prev_fiber->swap_site
    print g_current_fiber->prev_fiber->last_worker
    print g_current_fiber->prev_fiber->fn
    print g_current_fiber->prev_fiber->magic1
    print g_current_fiber->prev_fiber->magic2
  end
end

# Check each worker thread
echo \n--- Thread 2 fiber info ---\n
thread 2
print g_current_fiber
if g_current_fiber != 0
  print g_current_fiber->id
  print g_current_fiber->swap_caller
  print g_current_fiber->swap_site
  print g_current_fiber->prev_fiber
  print g_current_fiber->last_worker
  print g_current_fiber->magic1
  print g_current_fiber->magic2
end

echo \n--- Thread 3 fiber info ---\n
thread 3
print g_current_fiber
if g_current_fiber != 0
  print g_current_fiber->id
  print g_current_fiber->swap_caller
  print g_current_fiber->swap_site
  print g_current_fiber->prev_fiber
  print g_current_fiber->last_worker
  print g_current_fiber->magic1
  print g_current_fiber->magic2
end

echo \n--- Thread 4 fiber info ---\n
thread 4
print g_current_fiber
if g_current_fiber != 0
  print g_current_fiber->id
  print g_current_fiber->swap_caller
  print g_current_fiber->swap_site
  print g_current_fiber->prev_fiber
  print g_current_fiber->last_worker
  print g_current_fiber->magic1
  print g_current_fiber->magic2
end

echo \n--- Thread 5 fiber info ---\n
thread 5
print g_current_fiber
if g_current_fiber != 0
  print g_current_fiber->id
  print g_current_fiber->swap_caller
  print g_current_fiber->swap_site
  print g_current_fiber->prev_fiber
  print g_current_fiber->last_worker
  print g_current_fiber->magic1
  print g_current_fiber->magic2
end

echo \n--- Thread 6 fiber info ---\n
thread 6
print g_current_fiber
if g_current_fiber != 0
  print g_current_fiber->id
  print g_current_fiber->swap_caller
  print g_current_fiber->swap_site
  print g_current_fiber->prev_fiber
  print g_current_fiber->last_worker
  print g_current_fiber->magic1
  print g_current_fiber->magic2
end

quit
