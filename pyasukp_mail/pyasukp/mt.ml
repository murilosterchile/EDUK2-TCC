(* Pour comparer aux temps annoncés par caccetta *)

open Globals
open Eduk
open Wandp.M
open Init
open Datastruct
let put_data ch comment n c w p =
  try
    output_string ch ("##\n");
    output_string ch ("##"^comment^"\n\n");
    output_string ch ("n: "^(string_of_int n)^"\n\n");
    output_string ch ("c: ");
    output_string ch ((string_of_weight c)^(String.make 30 ' ')^"\n\n");
    output_string ch ("begin data \n");
    for i = 1 to n do
      output_string ch ((string_of_weight w.(i))^"\t"^(string_of_profit p.(i))^"\n")
    done;
    output_string ch ("end data \n");
    flush ch;
    close_out ch
  with e -> (prerr_endline "Error in put_data";raise e)
;;
let read_first_float ch =
  let fl = ref true in
  let res = ref "" in
  while !fl do
    let s = input_line ch in
    let i = ref 0 in
    while !i < String.length s && !fl do
      if s.[!i] >= '0' && s.[!i] <= '9' then
        fl := false
      else incr i
    done;
    res := String.sub s !i (String.length s - !i);
  done;
  let i = ref 0 in
  let s = !res in
  fl := true;
  while !i < String.length s && !fl do
    if (s.[!i] >= '0' && s.[!i] <= '9') || s.[!i] = '.' then
      incr i
    else fl := false
  done;
  float_of_string (String.sub s 0 !i)

    let one_instance f wmin wmax pmin pmax n step =
      let wmat, pmat = f wmin wmax pmin pmax n step in
      let div = if n *wmax > 10000000 then 10 else 2 in
      let c = min (max_int/2) (Array.fold_left (fun x y -> if x > max_int/2 then x + 1 else x + y/div ) 0 wmat) in
      let in_ch,out_ch = Unix.open_process "../MT/MT/mtu2b" in
      put_data out_ch "" n c wmat pmat;
      flush out_ch;    
     let t =read_first_float in_ch in
     close_in in_ch; close_out out_ch; 
t
  
        ;;

let nb_instances f wmin wmax pmin pmax n step nb=
 let rt = ref 0. in
 for i = 1 to nb do
   rt :=  (+.) !rt (one_instance f wmin wmax pmin pmax n step);
  Unix.wait()
 done;
 List.map (fun x -> x /. (float nb)) [!rt]

let unssp nb=
  let n = [100;300;500;1000;3000;5000;10000;30000;50000;100000;300000;500000;1000000] in
  let wmax = [1000;10000] in
  let f wmin wmax pmin pmax n step =  Datagen.unrsubset n 10 wmax 1 false in
  List.iter (fun wmax ->   Printf.printf "\nSubset Sum PB \nwmax:%d wmin:%d\n" wmax 10; flush stdout;
    List.iter
      (fun n ->      Printf.printf "\n%d\t" n;
        let rt = nb_instances f () wmax () () n () nb in
        List.iter (Printf.printf "%f\t") rt; flush stdout) n)wmax
    
let unscp nb=
  let n = [100;300;500;1000;3000;5000;10000;30000;50000;100000;300000;500000;1000000] in
  let wmax = [1000;10000] in
  let step =[10;100;1000] in
  let f wmin wmax pmin pmax n step =  Datagen.strongcor n 10 wmax step in
  Printf.printf "MTU2\n"; flush stdout;
   List.iter
     (fun n ->      Printf.printf "\n%d\t" n;
       List.iter (fun wmax -> Printf.printf"\n Strong Correletated wmin=%d wmax= %d\n \t alpha=0 \t\t alpha=100\t\t alpha = 1000\n" 10 wmax; flush stdout;
         List.iter (fun step -> 
           let rt = nb_instances f () wmax () () n step nb in
           List.iter (fun t -> Printf.printf "%f\t" t; flush stdout) rt; flush stdout) step)
         wmax)
    n

let saw nb=
  let n = [100;300;500;1000;3000;5000;10000;30000;50000;100000;300000;500000;1000000] in
  let wmax = [1000;10000;100000] in
  let wmin = [10;1000;10000] in
  let step =[0;100;1000] in
  let f wmin wmax pmin pmax n step =  Datagen.varap n wmin (wmin+wmax) step in
  List.iter
    (fun wmin ->
      List.iter 
        (fun wmax ->
      Printf.printf "\nSaw-UKP instances wmin=%d wmax= %d\n \t alpha=0 \t alpha=100\t alpha = 1000\n" wmin (wmax+wmin); flush stdout;
      Printf.printf "n \n"; flush stdout;
          (List.iter
             (fun n -> if n > (wmax/2) then () else begin     Printf.printf "\n%d\t" n;
               List.iter 
                 (fun step -> 
                   let rt = nb_instances f wmin wmax () () n step nb in
                   List.iter 
                     (fun t -> Printf.printf "%f\t" t; flush stdout) 
                     rt
                 ) 
                 step 
             end)
             n
          ))
         wmax )
        wmin
let realistic nb=
  let n = [100;300;500;1000;3000;5000;10000;30000;50000;100000;300000;500000;1000000] in
  let wmax = [1000;10000;100000] in
  let wmin = [10;100;1000;10000] in
  let f wmin wmax pmin pmax n step =  Datagen.nosimpledom2 n wmin (10*wmin+wmax) wmin (10*wmin+wmax) in
  List.iter
    (fun wmin ->
      List.iter 
        (fun wmax ->
      Printf.printf "\nRealistic instances wmin= %d wmax= %d \n " wmin (10*wmin+wmax); flush stdout;
      Printf.printf "n\t MTU2 \n"; flush stdout;
          (List.iter
             (fun n -> if n > (wmax/5) then () else begin     Printf.printf "\n%d\t" n; flush stdout;
               let rt = nb_instances f wmin wmax () () n () nb in
               List.iter 
                 (fun t -> Printf.printf "%f\t" t; flush stdout) 
                 rt
             end)
             n
          ))
        wmax )
    wmin

let realhard nb=
  let n = [100;300;500;1000;3000;5000;10000] in
  let wmax = [1000;10000;100000] in
  let wmin = [10;100;1000;] in
  let f wmin wmax pmin pmax n step =  Datagen.realhard n wmin  (10*wmin+wmax)  in
  List.iter
    (fun wmin ->
      List.iter 
        (fun wmax ->
      Printf.printf "\nRealhard instances wmin=%d wmax= %d\n " wmin (10*wmin+wmax); flush stdout;
      Printf.printf "n\t MTU2\n"; flush stdout;
          (List.iter
             (fun n -> if n > (wmax/2) then () else begin     Printf.printf "\n%d\t" n;
               let rt = nb_instances f wmin wmax () () n () nb in
               List.iter 
                 (fun t -> Printf.printf "%f\t" t; flush stdout) 
                 rt
             end)
             n
          ))
        wmax )
    wmin



let _ =
  Sys.catch_break true; 
  unssp 50;
  unscp 50;
 realistic 5;
  saw 50;
  realhard 50;

