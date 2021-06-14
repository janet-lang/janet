###
### Rule implementation
###
### Also contains wrappers to more easily define rules in an
### incremental manner.
###

(import ./dagbuild)
(import ./shutil)

(defn- executor
  "How to execute a rule at runtime -
  extract the recipe thunk(s) and call them."
  [rule]
  (if-let [r (get rule :recipe)]
    (try
      (if (indexed? r)
        (each rr r (rr))
        (r))
      # On errors, ensure that none of the output file for this rule
      # are kept.
      ([err f]
       (each o (get rule :outputs [])
         (protect (shutil/rm o)))
       (propagate err f)))))

(defn- target-not-found
  "Creates an error message."
  [target]
  (errorf "target %v does not exist and no rule exists to build it" target))

(defn- target-already-defined
  "Error when an output already has a rule defined to create it."
  [target]
  (errorf "target %v has multiple rules" target))

(defn- utd
  "Check if a target is up to date.
  Inputs are guaranteed to already be in the utd-cache."
  [target all-targets utd-cache] 
  (def rule (get all-targets target))
  (if (= target (get rule :task)) (break false))
  (def mtime (os/stat target :modified))
  (if-not rule (break (or mtime (target-not-found target))))
  (if (not mtime) (break false))
  (var ret true)
  (each i (get rule :inputs [])
    (if-not (get utd-cache i) (break (set ret false)))
    (def s (os/stat i :modified))
    (when (or (not s) (< mtime s))
      (set ret false)
      (break)))
  ret)

(defn build-rules
  "Given a graph of all rules, extract a work graph that will build out-of-date
  files."
  [rules targets &opt n-workers]
  (def dag @{})
  (def utd-cache @{})
  (def all-targets @{})
  (def seen @{})
  (each rule (distinct rules)
    (when-let [p (get rule :task)]
      (when (get all-targets p) (target-already-defined p))
      (put all-targets p rule))
    (each o (get rule :outputs [])
      (when (get all-targets o) (target-already-defined o))
      (put all-targets o rule)))

  (defn utd1
    [target]
    (def u (get utd-cache target))
    (if (not= nil u)
      u
      (set (utd-cache target) (utd target all-targets utd-cache))))

  (defn visit [target]
    (if (in seen target) (break))
    (put seen target true)
    (def rule (get all-targets target))
    (def inputs (get rule :inputs []))
    (each i inputs
      (visit i))
    (def u (utd1 target))
    (unless u
      (def deps (set (dag rule) (get dag rule @[])))
      (each i inputs
        (unless (utd1 i)
          (if-let [r (get all-targets i)]
            (array/push deps r))))))

  (each t targets (visit t))
  (dagbuild/pdag executor dag n-workers))

#
# Convenience wrappers for defining a rule graph.
# Must be mostly compatible with old jpm interface.
# Main differences are multiple outputs for a rule are allowed,
# and a rule cannot have both phony and non-phony thunks.
#

(defn getrules []
  (if-let [targets (dyn :rules)] targets (setdyn :rules @{})))

(defn- gettarget [target]
  (def item ((getrules) target))
  (unless item (error (string "no rule for target '" target "'")))
  item)

(defn- target-append
  [target key v]
  (def item (gettarget target))
  (def vals (get item key))
  (unless (find |(= v $) vals)
    (array/push vals v))
  item)

(defn add-input
  "Add a dependency to an existing rule. Useful for extending phony
  rules or extending the dependency graph of existing rules."
  [target input]
  (target-append target :inputs input))

(defn add-dep
  "Alias for `add-input`"
  [target dep]
  (target-append target :inputs dep))

(defn add-output
  "Add an output file to an existing rule. Rules can contain multiple
  outputs, but are still referred to by a main target name."
  [target output]
  (target-append target :outputs output))

(defn add-thunk
  "Append a thunk to a target's recipe."
  [target thunk]
  (target-append target :recipe thunk))

(defn- rule-impl
  [target deps thunk &opt phony]
  (def targets (getrules))
  (unless (get targets target)
    (def new-rule
      @{:task (if phony target)
        :inputs @[]
        :outputs @[]
        :recipe @[]})
    (put targets target new-rule))
  (each d deps (add-input target d))
  (unless phony
    (add-output target target))
  (add-thunk target thunk))

(defmacro rule
  "Add a rule to the rule graph."
  [target deps & body]
  ~(,rule-impl ,target ,deps (fn [] nil ,;body)))

(defmacro task
  "Add a task rule to the rule graph. A task rule will always run if invoked
  (it is always considered out of date)."
  [target deps & body]
  ~(,rule-impl ,target ,deps (fn [] nil ,;body) true))

(defmacro phony
  "Alias for `task`."
  [target deps & body]
  ~(,rule-impl ,target ,deps (fn [] nil ,;body) true))

(defmacro sh-rule
  "Add a rule that invokes a shell command, and fails if the command returns non-zero."
  [target deps & body]
  ~(,rule-impl ,target ,deps (fn [] (,shutil/shell (,string ,;body)))))

(defmacro sh-task
  "Add a task that invokes a shell command, and fails if the command returns non-zero."
  [target deps & body]
  ~(,rule-impl ,target ,deps (fn [] (,shutil/shell (,string ,;body))) true))

(defmacro sh-phony
  "Alias for `sh-task`."
  [target deps & body]
  ~(,rule-impl ,target ,deps (fn [] (,shutil/shell (,string ,;body))) true))

(defmacro add-body
  "Add recipe code to an existing rule. This makes existing rules do more but
  does not modify the dependency graph."
  [target & body]
  ~(,add-thunk ,target (fn [] ,;body)))
