(* Pour comparer aux temps annoncés par caccetta *)
let message s =  ()
(*(prerr_endline ("point: "^( string_of_int s));flush stderr)*)
open Globals
open Eduk
open Wandp.M
open Init
open Datastruct
open Thread

let lt = 2.
let pas = 0.01
let rnbc = ref 50
let wpmax = truncate(sqrt(float max_int))
let cpt = ref 0

let change_info r default i1 i2 i =
if i = i2 then r := i else
if i = i1 && !r = default then r := i
else ()

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
     
exception Err of string
exception MT_too_long
    
let rec active_wait check pas tot =
  if tot <= 0. then () else
  if check () then () else
  begin
    Thread.delay pas; active_wait check pas (tot -. pas)
  end
    
let one_instance_mt wmat pmat c n file =
  Edukio.put_data file "" n c wmat pmat ;
  message 10;
  let resf = file^"r" in
  let chin = (Unix.openfile file [Unix.O_RDONLY] 0o777)  in
  message 11;
  if Sys.file_exists resf then Unix.unlink resf;
  let chout= (Unix.openfile resf [Unix.O_CREAT;Unix.O_RDWR;Unix.O_TRUNC] 0o777) in
  let pcs = "../MT/MT/mtu2b" in
  let args = [||] in
  let mut = Mutex.create() in
  let mutfile = Mutex.create () in
  let cond = Condition.create() in
  let pid = ref 0 in
  let pidsleep = ref 0 in
  let flag = ref false in
  let stat = ref None in
  message 12;
  let th_pcs = 
    Thread.create 
      (fun () -> 
        Mutex.lock mutfile;
        let p = 
          Unix.handle_unix_error 
            (Unix.create_process pcs args chin chout) (Unix.stderr)
        in 
        pid := p;
        message 12;
        Mutex.unlock mutfile;
        let _,st = Thread.wait_pid p in
        message 13;
        Mutex.lock mut;
        stat := Some st; flag := true ;  Condition.signal cond; Mutex.unlock mut ) ()
  in
  let delayth =
    Thread.create (fun d ->
      active_wait (fun () -> 
        Mutex.lock mut;
        let b = match !stat with None -> false | Some _ -> true in
        Mutex.unlock mut;b) pas d;
      Mutex.lock mut; flag := true ; 
      Condition.signal cond; Mutex.unlock mut ) lt
  in
  Mutex.lock mut;
  while not !flag do
    Condition.wait cond mut;
  done;
  Mutex.unlock mut;
  Unix.close chin;
  Unix.close chout;
  match !stat with
  | None ->
      begin
        Mutex.unlock mutfile;
        Unix.kill !pid Sys.sigkill;
        if Sys.file_exists file then Unix.unlink file;
        Thread.join th_pcs; Sys.remove resf;
        (-.(10.**4.),0),"mt too long";raise MT_too_long;
      end
  | _ ->    
      begin
        message 18;
        let ch_pipe_in = Unix.openfile resf [Unix.O_RDONLY] 0 in
        message 19;
	let ch = Unix.in_channel_of_descr ch_pipe_in in
        message 20;
        let t = read_first_float ch   in
        let z =int_of_float(read_first_float ch) in 
        message 21;
        if Sys.file_exists file then Unix.unlink file;
        close_in ch;
        Unix.unlink resf;
        Thread.join delayth;
        (t,z),"mt"
          
      end
        
let one_instance tmpfile fmt f wmin wmax pmin pmax n nbc =
  let diff = ref false in
  let rinfo = ref "" in
  let wmat, pmat = f n wmin wmax pmin pmax  in
  let over_flow = ref false in
  let initc =  (wmax+ 2* wmin)   in
  let stepc = wmax / nbc in
  let sigmaw = Array.fold_left (fun s e -> s+e) 0 wmat in
  let limitc = sigmaw/10 in
  let rc = ref initc in
  let lr = ref[ 0.; 0.; 0.; 0.;0.;0.] in
  for i = 1 to nbc do
    let one (wbb,pp,wab) =
      Init.refresh();
      try
        let tfwd, rescomputation =
          Timing.timing  (fun (wbb,pp,wab) -> 
            Parsecl.nbmaxsol := wbb;
            Parsecl.prepro := pp;
            message i;
            Eduk.forward wmat pmat !rc wab wab) (wbb,pp,wab)
        in
        message (-i);
        if not !(Bounds.messbigint) then over_flow := true;
        let tback,sol=
          match rescomputation with
          | Normal ((wseq,pseq,_), 
                    period_level_reached, 
                    best,
                    nb_best, starting_backtrack_point, last_w_comp, wreduct) ->
                      Timing.timing 
                        (fun () ->
                          let initsol = if nb_best = 0 then [] else 
                          [(Astore.get iteminfos.item best,nb_best)] in
                          Eduk.agregat(Eduk.backtrack wmat pmat starting_backtrack_point 0 initsol) )()
          | With_bound ( bound, ((wpt,ppt,ipt) as cp),solut) -> 
              Timing.timing 
                (fun () -> Eduk.agregat(Eduk.backtrack_with_bound wmat pmat ( bound, cp,solut)))()
        in 
        let (wopt,popt) = Eduk.accumulate wmat pmat sol in
        (tfwd +. tback,popt),""
      with e -> (Edukio.put_data "bug.ukp" "" n !rc wmat pmat;raise e)
    in 
    let tsols1=
      (List.map one 
         [(10000,true,true);
          (10000,false,true);
          (0,true,true);
          (0,false,true);
          (0,false,false);])in
    let vmt = 
      if not !over_flow then
        try !fmt wmat pmat !rc n (tmpfile^(string_of_int i)) with
          MT_too_long -> begin 
            fmt := (fun _ _ _ _ _ ->         (-.(10.**5.),0),"mt too long");
            (-.(10.**5.),0),"mt too long" end 
      else begin 
        fmt := (fun _ _ _ _ _ ->         (-.(10.**5.),0),"int_over_flow");
        (-.(10.**5.),0),"int_over_flow"
      end
    in
    let tsols = tsols1@[vmt] in
    let ((t,v),_)::suite = tsols in
    List.iter (fun ((_,u),s) -> 
      if u<>v then 
        begin diff:= true;
          change_info rinfo ""  "int_over_flow" "mt too long" s
        end) suite;
    if !diff && (!rinfo = "")
    then begin incr cpt; 
      List.iter (fun ((u,v),s) -> prerr_string (s^" ");prerr_float u;prerr_string" ";prerr_int v;prerr_string"\n";flush stderr) tsols;
      Edukio.put_data ("diffsolver"^tmpfile^(string_of_int !cpt)^".ukp")"" n !rc wmat pmat
    end;
    lr :=   List.map2 (+.) !lr (fst(List.split(fst(List.split tsols))));
    rc := 
      if !rc < limitc + wmin then !rc + (stepc +Random.int (stepc))
      else initc + Random.int (limitc - initc) ;
  done;
  (List.map (fun i -> i /. (float nbc)) !lr, !diff, !rinfo)
;;


let nb_instances tmpfile f wmin wmax pmin pmax n nb =
  let rt = ref [0.;0.;0.;0.;0.;0.] in
  let diff = ref false in
  let rinfo = ref "" in
  for i = 1 to nb do
    let fmt = ref one_instance_mt in
    let l,d,ri = (Unix.handle_unix_error(one_instance tmpfile fmt f wmin wmax pmin pmax n ) !rnbc) in
    rt := List.map2 (+.) !rt l;
    diff := !diff || d;
      change_info rinfo "" "int_over_flow" "mt too long" ri
  done;
  List.map (fun x -> x /. (float nb)) !rt, !diff, !rinfo
    
    
let _ = 
  let data n wmin wmax arg3 arg4  = match Sys.argv.(1) with
  | "ss2" ->   Datagen.subset n wmin wmax 2 false
  | "ss5" ->   Datagen.subset n wmin wmax 5 false
  | "unrss"|"ss" -> Datagen.unrsubset n wmin wmax 1 false
  | "sc"  | "scbis" -> Datagen.strongcor n wmin wmax arg3
  |  "saw" -> Datagen.saw n wmin wmax 10
  |  "unrsaw" -> Datagen.unr_saw n wmin wmax arg3 arg4 10
  | "real" -> Datagen.nosimpledom2 n wmin wmax arg3 arg4
  | "rh" -> Datagen.realhard n wmin wmax
  | "hi" -> Datagen.hardincreasingratio n wmin wmax arg3 arg4
  | "hd" -> Datagen.harddecreasingratio n wmin wmax arg3 arg4
  | "w" -> Datagen.weakcor n wmin wmax arg3
  | "chung" -> Datagen.chung n wmin arg3
  in 
  let n = int_of_string Sys.argv.(2) in
  let wmin = int_of_string Sys.argv.(3) in
  let wmax = int_of_string Sys.argv.(4) in
  let arg3 = int_of_string Sys.argv.(5) in
  let arg4 = int_of_string Sys.argv.(6) in
  let nb = int_of_string Sys.argv.(7) in
  let nbc = int_of_string Sys.argv.(8) in
  let tmpfile = "tmp"^Sys.argv.(1)^".ukp" in
  rnbc := nbc;
  if Sys.argv.(1) <> "unrss" && Sys.argv.(1) <> "unrsaw" &&Sys.argv.(1) <> "hi" &&Sys.argv.(1) <> "hd" &&Sys.argv.(1) <> "w" && Sys.argv.(1) <> "sc"&& Sys.argv.(1) <> "scbis" && wmax - wmin < 2 * n then () else begin
    try
      let lres,diff,rinfo = nb_instances tmpfile data wmin wmax arg3 arg4 n nb  in
      Printf.printf "%d\t" n;
      List.iter (Printf.printf "%f\t") lres;
      Printf.printf "%b\t" diff;
      Printf.printf "% s\n" rinfo;
    with e -> (prerr_endline (Printexc.to_string e);flush stderr; raise e)
  end
;;
