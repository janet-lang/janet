###
### dagbuild.janet
###
### A module for building files / running commands in an order.
### Building blocks for a Make-like build system.
###

#
# DAG Execution
#

(defn pmap
  "Function form of `ev/gather`. If any of the
  sibling fibers error, all other siblings will be canceled.  Returns the gathered
  results in an array."
  [f data]
  (def chan (ev/chan))
  (def res @[])
  (def fibers
    (seq [[i x] :pairs data]
      (ev/go (fiber/new (fn [] (put res i (f x))) :tp) nil chan)))
  (repeat (length fibers)
    (def [sig fiber] (ev/take chan))
    (unless (= sig :ok)
      (each f fibers (ev/cancel f "sibling canceled"))
      (propagate (fiber/last-value fiber) fiber)))
  res)

(defn pdag
  "Executes a dag by calling f on every node in the graph.
  Can set the number of workers
  for parallel execution. The graph is represented as a table
  mapping nodes to arrays of child nodes. Each node will only be evaluated
  after all children have been evaluated. Returns a table mapping each node
  to the result of `(f node)`."
  [f dag &opt n-workers]

  # preprocess
  (def res @{})
  (def seen @{})
  (def q (ev/chan math/int32-max))
  (def dep-counts @{})
  (def inv @{})
  (defn visit [node]
    (if (seen node) (break))
    (put seen node true)
    (def depends-on (get dag node []))
    (if (empty? depends-on)
      (ev/give q node))
    (each r depends-on
      (put inv r (array/push (get inv r @[]) node))
      (visit r)))
  (eachk r dag (visit r))

  # run n workers in parallel
  (default n-workers (max 1 (length seen)))
  (assert (> n-workers 0))
  (defn worker [&]
    (while (next seen)
      (def node (ev/take q))
      (if-not node (break))
      (when (in seen node)
        (put seen node nil)
        (put res node (f node)))
      (each r (get inv node [])
        (when (zero? (set (dep-counts r) (dec (get dep-counts r 1))))
          (ev/give q r))))
    (ev/give q nil))

  (pmap worker (range n-workers))
  res)
