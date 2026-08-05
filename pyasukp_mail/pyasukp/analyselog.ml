(* Pour recuperer les infos d'un script d'execution de ukp *)

let pyascmd = ["pya_old";"pya_old_5000";"pya_new";"pya_new_5000";"pya_new_U3";"pya_new_both";"eduk"]
let pyascmd = ["pya_old";"pyanv1";"pyanv2";"pyanv12";"pya_U3";"pya_both";"eduk"]
let pyascmdshort = ["pya_old_5000";"pya_new_5000";"pya_new_U3";"eduk"]
let pyas_init = List.map (fun _ -> (0.,0,0,0)) pyascmd

type int_or_float = Sint of int | Sfloat of float

let écart r v = if r > 0. then (v -. r) /. r else 0.
let analyse_command s = Scanf.sscanf s  "./pyasukpt -form %s -wmin %d -pmin %d -n %d -wmax %d -seed %d" (fun f wmin p n wmax s -> (wmin,wmax,n))
let analyse_i s = Scanf.sscanf s  "%d" (fun i -> i)
let analyse_pya2 s = 
 try 
   Scanf.sscanf s "Not a Saw ukp %d	%B	         %d	         %d	%B	 %d	 %7f   %d" 
     (fun nbu periodReache rnbthresh_und  nbcrtpt wbigint prof temps nbn->  (false,nbu, periodReache, rnbthresh_und,  nbcrtpt, wbigint, prof, temps,nbn))
 with _ -> try
 Scanf.sscanf s "no bound          %d	%B	         %d	         %d	%B	 %d	 %7f   %d" 
     (fun nbu periodReache rnbthresh_und  nbcrtpt wbigint prof temps nbn ->  (true,nbu, periodReache, rnbthresh_und,  nbcrtpt, wbigint, prof, temps, nbn))
 with _ -> 
   Scanf.sscanf s "Saw ukp %d	%B	         %d	         %d	%B	 %d	  %7f  %d" 
     (fun nbu periodReache rnbthresh_und  nbcrtpt wbigint prof temps nbn -> (true,nbu, periodReache, rnbthresh_und,  nbcrtpt, wbigint, prof, temps, nbn))

let analyse_mtu2 s=
 try Scanf.sscanf s "%d" (fun i -> Sint i)
 with _ -> Scanf.sscanf s "%s %6f" (fun s i -> Sfloat i)
let analyse_mtu2v s =
 Scanf.sscanf s "%s %d;" (fun s i -> i)

let prof_pya2 v = match v with 
Some (_,nbu, periodReache, rnbthresh_und,  nbcrtpt, wbigint, prof, temps,nbn) -> prof
| None -> failwith "prof_pya2"
let temps_and_in_DP__pya2  v = match v with
| Some (_,nbu, periodReache, rnbthresh_und,  nbcrtpt, wbigint, prof, temps,nbn) -> temps, 0,(if nbcrtpt > 1 then 1 else 0),nbn
|None -> 0.,1,0,0


let analyse_a s = Scanf.sscanf s "%s" (fun v -> v = "a") 


let analyse_instance_2 nb ch_in  =
  let ana_pya () = let s = input_line ch_in in
    try 
      let v = analyse_pya2 s in
      let b = analyse_a (input_line ch_in) in
      Some v
    with _ -> None
  in
  try
  let solver1 = analyse_command (input_line ch_in) in
  let pya_old = ana_pya () in
  let pya_old_5000 = ana_pya () in
  let pya_10000 = ana_pya () in
  let pya_5000 = ana_pya () in
  let pya_u3_5000 = ana_pya () in
  let pya_uv_5000 = ana_pya () in
  let pya_eduk = ana_pya () in
  let mtu2,next = try begin
    match analyse_mtu2 (input_line ch_in) with
    | Sint i -> None, i
    | Sfloat f -> let pr = analyse_mtu2v (input_line ch_in) in 
      let i = (try analyse_i (input_line ch_in) with End_of_file -> -1)
      in Some(pr,f),i
  end
      with End_of_file -> None, -1
 in
 (solver1,pya_old,pya_old_5000,pya_10000,pya_5000,pya_u3_5000,pya_uv_5000,pya_eduk,mtu2,next)
 with e -> (print_endline (string_of_int nb);flush stdout;raise e)
 
let analyse_fichier_log2 nf =
  let ch = open_in nf in
  ignore(input_line ch);
  let rec aux oldsolver1 resc res nb =
    let   r = try Some(analyse_instance_2 nb ch) with End_of_file -> None in
    match r with 
      None -> (resc::res)
    | Some ( (solver1,pya_old,pya_old_5000,pya_10000,pya_5000,pya_u3_5000,pya_uv_5000,pya_eduk,mtu2,next) as v) ->
    if oldsolver1 <> solver1 then aux solver1 [v] (resc::res) (nb+10)
    else if next = -1 then (( v::resc)::res)
    else aux solver1 ( v::resc) res (nb+10)
  in let lr = aux (100,11000,1000) [] [] 0 in
  begin close_in ch; lr end



type totaux = 
    { pyas : (float * int * int *int ) list;
      mtu2: float;
      nb_mtu2_killed: int;
      nb_mtu2_out: int;
      nb_tot: int;
      wmin : int;
      wmax : int;
      n : int
}

let rec comb_pyas l1 l2 = match (l1,l2) with
| [],[] -> []
| (t,n,nb,nbn)::r, (s,m,mb,mbn)::q -> (t+.s,n+m,nb+mb,nbn+mbn):: comb_pyas r q
| _ -> assert false

let augmente_totaux2 (tot,nb) ((wmi,wma,n),pya_old,pya_old_5000,pya_10000,pya_5000,pya_u3_5000,pya_uv_5000,pya_eduk,mtu2,next) =
  let tmtu2,nkmtu2,nomtu2 = match mtu2 with
  |None -> 0.,1,0
  | Some(pr,tps) when pr = (prof_pya2 pya_5000) -> tps,0,0
  | _ -> 0.,0,1
 in
 let pyastmp = [pya_old; pya_old_5000; pya_10000; pya_5000; pya_u3_5000; pya_uv_5000; pya_eduk] in
 let pyas = List.map temps_and_in_DP__pya2 pyastmp in

(    { pyas = comb_pyas tot.pyas pyas;
      mtu2 = tot.mtu2 +. tmtu2;
      nb_mtu2_killed = tot.nb_mtu2_killed + nkmtu2;
      nb_mtu2_out = tot.nb_mtu2_out + nomtu2;
      nb_tot = 1 + tot.nb_tot;
      wmin = wmi;
      wmax = wma;
      n = n;
},nb+1)

let calcule_totaux2 lres =
  List.fold_left augmente_totaux2 
    (    { pyas= pyas_init;
	    mtu2 = 0.;
	    nb_mtu2_killed = 0;
	    nb_mtu2_out = 0;
	    nb_tot = 0;
	    wmin = 100;
	    wmax = 11000;
	    n = 1000;
	  },0)
       lres

let entete = 
 List.fold_right (fun s t -> s^"\tNBPD\tNBN\t"^t)   pyascmd "MTU2\ttot\tkil\tout\twmin\twmax\tn"

let tot_to_string tot = 
 let div (f,nb) = if nb = tot.nb_tot then -1. else f /. (float (tot.nb_tot - nb)) in
 List.fold_right (fun (t,nb,nbpd,npn) s -> (Printf.sprintf "%.4f\t%d\t%d\t" (div (t,nb)) nbpd (if nb = tot.nb_tot then -1 else npn/(tot.nb_tot - nb)))^s)
   tot.pyas
   (Printf.sprintf "%.4f \t %d %d %d %d %d %d"
      ( let nbv = (tot.nb_tot - tot.nb_mtu2_killed - tot.nb_mtu2_out) in if nbv = 0 then -1. else tot.mtu2 /. (float nbv))
      tot.nb_tot tot.nb_mtu2_killed tot.nb_mtu2_out tot.wmin tot.wmax tot.n)

let to_rapp file =
 let lres = analyse_fichier_log2 file in
 let ltot = List.map calcule_totaux2 lres in
 let ch_out = open_out (file^".txt") in
 Printf.fprintf ch_out "%s\n" entete;
 List.iter (fun (t,_) -> Printf.fprintf ch_out "%s\n" (tot_to_string t)) ltot;
 close_out ch_out

let tot_to_string_short tot = 
 let [pya_old;pya_old_5000;pya_new;pya_new_5000;pya_new_5000_U3;pya_new_5000_UV;eduk] = tot.pyas in
 let totshort = [pya_old_5000; pya_new_5000; pya_new_5000_U3;eduk]  in
 let div (f,nb) = f /. (float (tot.nb_tot - nb)) in
 List.fold_right (fun (t,nb,nbpd,nbn) s -> (Printf.sprintf "%.4f  \t " (div (t,nb)) )^s)
   totshort
   (Printf.sprintf "%.4f \t %d %d %d %d %d %d"
      ( let nbv = (tot.nb_tot - tot.nb_mtu2_killed - tot.nb_mtu2_out) in if nbv = 0 then -1. else tot.mtu2 /. (float nbv))
      tot.nb_tot tot.nb_mtu2_killed tot.nb_mtu2_out tot.wmin tot.wmax tot.n)

let entete_short = 
 List.fold_right (fun s t -> s^" \t  "^t)   pyascmdshort "MTU2    \t tot  \t kil \t out \t wmin \t wmax \t n"

let to_rapp_short file =
 let lres = analyse_fichier_log2 file in
 let ltot = List.map calcule_totaux2 lres in
 let ch_out = open_out (file^".txts") in
 Printf.fprintf ch_out "%s\n" entete_short;
 List.iter (fun (t,_) -> Printf.fprintf ch_out "%s\n" (tot_to_string_short t)) ltot;
 close_out ch_out


let entete2 = 
 List.fold_right (fun s t -> s^"\t"^t)   pyascmd "wmin\twmax\tn\tecpyanv1\tecpyanv2\tecpyanv12\tecpya_U3"

let fst3 (a,_,_,_) = a
let all_instances file =
 let lres = analyse_fichier_log2 file in
 let ch_out = open_out (file^".inst.txt") in
 Printf.fprintf ch_out "%s\n" entete2;
 List.iter (fun l ->
 List.iter 
 (fun ((wmi,wma,n),pya_old,pya_old_5000,pya_10000,pya_5000,pya_u3_5000,pya_uv_5000,pya_eduk,mtu2,next) ->
       Printf.fprintf ch_out "%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%d\t%d\t%d\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\n"
     (fst3(temps_and_in_DP__pya2 pya_old)) (fst3(temps_and_in_DP__pya2 pya_old_5000)) (fst3(temps_and_in_DP__pya2 pya_10000)) (fst3(temps_and_in_DP__pya2 pya_5000)) (fst3(temps_and_in_DP__pya2 pya_u3_5000)) (fst3(temps_and_in_DP__pya2 pya_uv_5000)) (fst3(temps_and_in_DP__pya2 pya_eduk))   wmi  wma  n
(écart (fst3(temps_and_in_DP__pya2 pya_old)) (fst3(temps_and_in_DP__pya2 pya_old_5000)))
(écart (fst3(temps_and_in_DP__pya2 pya_old)) (fst3(temps_and_in_DP__pya2 pya_10000)))
(écart (fst3(temps_and_in_DP__pya2 pya_old)) (fst3(temps_and_in_DP__pya2 pya_5000)))
(écart (fst3(temps_and_in_DP__pya2 pya_old)) (fst3(temps_and_in_DP__pya2 pya_u3_5000)))
0.
) l ) lres;
close_out ch_out
