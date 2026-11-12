:defn O (\0 0) \0 0
:defn Y \(\1 (0 0)) \1 (0 0)

:defn true \\1
:defn false \\0
:defn not \0 false true 
:defn and \\1 0 false
:defn or \\1 true 0
:defn xor \\1 (not 0) 0

:defn zero \\0
:defn succ \\\1 (2 1 0)
:defn add \\\\3 1 (2 1 0)
:defn mul \\\2 (1 0)
:defn pow \\0 1
:defn isZero \0 (\false) true

:defn pred \\\2 (\\0 (1 3)) \1 \0
:defn sub \\0 pred 1
:defn le \\isZero (sub 1 0)
:defn lt \\isZero (sub (succ 1) 0)
:defn ge \\isZero (sub 0 1)
:defn gt \\isZero (sub (succ 0) 1)
:defn eq \\and (le 1 0) (le 0 1)
:defn min \\le 0 1 0 1
:defn min \\ge 0 1 1 0
:defn div Y \\\lt 1 0 zero (succ (2 (sub 1 0) 0))

:defn pair \\\0 2 1
:defn fst \0 true
:defn snd \0 false

:defn nil \\0
:defn cons \\ \\1 3 (2 1 0)
:defn snoc \\ \\2 1 (1 3 0)
:defn head \0 true nil
:defn tail \fst (0 (\\pair (snd 0) (cons 1 (snd 0))) (pair nil nil))
:defn map \\0 (\\cons (3 1) 0) nil
:defn len \0 \\(succ 0) zero
:defn at \\head (0 tail 1)
:defn cat \\1 cons 0
:defn append \\0 cons (cons 1 nil)
:defn range \\sub 0 1 (\cons (pred (head 0)) 0) (cons (pred 0) nil)

:defn arrow Y \\\\isZero 2 (0 1) (isZero 0 (\\1 0) (pred 0 (3 (pred 2) 1) 1))
