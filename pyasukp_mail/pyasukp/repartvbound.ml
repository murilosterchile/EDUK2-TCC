
let is_appliable w p c =
let ((remains_array,nbremain),x), _, _,_, _, _, _, bests, sort_of_bound = Init.structures w p c in
 sort_of_bound = Bounds.Sv


let rec accumulate_tests2 (wmin, wmax, pmin, pmax, n) res nb =
 if nb = 0 then res else begin
  Random.init ((Random.int 1000) + 10 + (truncate (1000.*.Sys.time ())));
  let w, p = Datagen.nosimpledom2 n wmin wmax pmin pmax in
  if is_appliable w p wmax then 
    accumulate_tests2 (wmin, wmax, pmin, pmax, n) (1+res) (nb-1) 
  else
    accumulate_tests2 (wmin, wmax, pmin, pmax, n) res (nb-1) 
end
;;
let rec accumulate_tests (wmin, wmax, pmin, pmax, n) res nb =
 if nb = 0 then res else begin
  Random.init ((Random.int 1000) + 10 + (truncate (1000.*.Sys.time ())));
  let w, p = Datagen.nosimpledom n wmin wmax pmin pmax in
  if is_appliable w p wmax then 
    accumulate_tests (wmin, wmax, pmin, pmax, n) (1+res) (nb-1) 
  else
    accumulate_tests (wmin, wmax, pmin, pmax, n) res (nb-1) 
end
;;

let min_maxs = [10,1000; 10, 5000; 10, 10000; 10, 20000; 10, 50000;  10, 100000; 100, 1000; 100,5000; 100, 10000;100, 20000; 100, 50000; 100, 100000; 1000, 5000; 1000, 10000;1000, 20000; 1000, 50000; 1000, 100000;  5000,10000; 5000, 20000; 5000, 50000;5000, 100000]
let mults = [2;5;10]
let nb =  [100; 75; 50; 25; 10]
let test_of_min_max mult (min,max) =
 List.map (fun i -> ((min,max,min*(1+mult),max+(mult*min), (max - min)/i),i)) nb
let array_tests mult = 
  (Array.of_list (List.flatten  (List.map (fun c -> test_of_min_max mult c) min_maxs)))

let print_one = (fun (((wmin, wmax, pmin, pmax, n),r),i) -> 
  Printf.printf "%d \t%d \t%d \t%f\n" pmin pmax i r;flush stdout)
let array_res accumulate_tests nb =
let rwmax = ref 0 in
List.iter
 (fun mult -> Printf.printf "\n#\n# pmin = (%d+1) x wmin \n# pmax = %d x wmin+  wmax \n# wmin \twmax \tl/n  \tprop\n#" mult mult;
   Array.iter (function ((wmin, wmax, pmin, pmax, n) as c, i) -> 
     if !rwmax <> wmax then begin
       Printf.printf "\n";
       Printf.printf "%d \t%d " wmin wmax ;flush stdout;
       rwmax := wmax
     end;
     let r = accumulate_tests c 0 nb in
     let prop =  (float r) /. (float nb)
     in Printf.printf "\t%f" prop;flush stdout;
     )
     (array_tests mult) ) mults


let _ = 
let nb = int_of_string (Sys.argv.(1)) in
(*
Printf.printf "\n#First data with  no simple dominance, but w.(1) may be greater than p.(1)\n#";
flush stdout;
array_res accumulate_tests nb ;
*)
Printf.printf "\n#Now data with  no simple dominance, but w.(1) is smaller than p.(1)\n#";
flush stdout;
array_res accumulate_tests2 nb ;

;; 
 
