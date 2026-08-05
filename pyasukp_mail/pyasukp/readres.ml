let chin = open_in "textr.txt" ;;
  let rec blanc_prec s i =
     if i= -1 or s.[i]=' ' or s.[i]='\t' then i else blanc_prec s (i-1)
  let read_vect ch =
    let rs = ref "" in
     (try while true do rs := input_line ch done with End_of_file -> ()) ;
    if !rs = "" then [||]
    else
    let s = !rs in
      let rec explode i res =
        if i<0 then res else
        let j = blanc_prec s i in
         let ns = (String.sub s (j+1) (i-j)) in if ns = "" then explode (j-1) res else explode (j-1) (ns::res)
      in
      Array.of_list(explode (String.length s -1) [])
;;

close_in chin;;
