let message s =  ()
(*(prerr_endline ("point: "^( string_of_int s));flush stderr)*)
open Globals
open Eduk
open Wandp.M
open Init
open Datastruct
open Thread

let ext = ref ".ukp"
let lt = 1800.
let pas = 0.01
let rnbc = ref 50
let wpmax = truncate(sqrt(float max_int))
let cpt = ref 0

let change_info r default i1 i2 i =
if i = i2 then r := i else
if i = i1 && !r = default then r := i
else if i = default then ()
else if !r = default then r := i
else r := !r^" "^i

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

  let rec blanc_prec s i =
     if i= -1 or s.[i]=' ' or s.[i]='\t' then i else blanc_prec s (i-1)
  let read_last_vect ch =
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
     
exception Err of string
exception MT_too_long
    
let active_wait check pas tot =
let rtot = ref tot in 
let rpas = ref pas in 
let maxpas = 100.*.pas in
  while !rtot > 0. && not ( check ()) do 
    Thread.delay !rpas; 
    rtot := (!rtot -. !rpas);
    rpas := (max maxpas (!rpas +. (max pas (!rpas /. 10.))));
    done
    
let one_instance_mt wmat pmat c n file =
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
        (try ignore(Unix.kill !pid Sys.sigkill) with _ -> ());
        if Sys.file_exists file then Unix.unlink file;
        Thread.join th_pcs; 
        if Sys.file_exists resf then Sys.remove resf;
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
         if Sys.file_exists resf then Unix.unlink resf;
        Thread.join delayth;
        (t,z),"mt"
          
      end
        
let one_instance tmpfile fmt f wmin wmax pmin pmax n nbc form=
  let diff = ref false in
  let rinfo = ref "def" in
  let wmat, pmat = f n wmin wmax pmin pmax form in
  let stepc = (wmax+wmin) / (2*nbc) in
  let sigmaw = Array.fold_left (fun s e -> min (max_int/2) (s+e)) 0 wmat in
  let limitc = sigmaw/10 in
  let initc =  limitc/2   in
  let rc = ref (if nbc >1 then initc else n*(n+1)*((n-1)/2) + n*((n-1)/2)) in
  let nbb = min n (max 100 (n/100)) in
  let lr = ref[ 0.; 0.; 0.; 0.;0.;0.] in
  let cptres = ref 1 in
(*  ignore(Sys.command "rm -f *.ukp");*)
  for i = 1 to nbc do
      diff:=false;
      if nbc > 1 then begin
        rc := 
          if !rc < limitc + wmin then !rc + (stepc +Random.int (stepc))
          else initc + Random.int (limitc - initc) ;
      end;
    let one (wbb,pp,wab) =
      Edukio.put_data tmpfile "" n !rc wmat pmat ;
      refresh();
      let tfwd, rescomputation,over =
        let thtmp =
          Thread.create 
            (fun () -> 
              let args = [|"./eduk05t "; "-src";tmpfile;if wab then "-wab" else "-nh";"-bblim";string_of_int wbb;if pp then "-pp" else "-nh"; "-batch";"-nbb";string_of_int nbb;"-res";tmpfile^"r"^(string_of_int !cptres)|] in
             (* Array.iter (fun s -> prerr_string (s^" ")) args;flush stderr;*)
              let p = 
                Unix.handle_unix_error 
                  (Unix.create_process "./eduk05t"  args Unix.stdin Unix.stdout) (Unix.stderr)
              in 
              let _,st = Thread.wait_pid p in
              true) () in
        Thread.join thtmp;
        let tmpr = (tmpfile^"r"^(string_of_int !cptres)) in
	if Sys.file_exists tmpr then begin
        let chin = open_in tmpr in
        try
        let rvect = read_last_vect chin in
        close_in chin;
         Sys.remove tmpr;
        incr cptres;
        float_of_string rvect.(Array.length rvect -1),int_of_string  rvect.(Array.length rvect - 2), rvect.(Array.length rvect - 3)
        with e -> (prerr_endline (Printexc.to_string e);flush stderr;Edukio.put_data (tmpfile^"bug"^(string_of_int !cpt)^".bug") tmpfile n !rc wmat pmat;(-100000.,1,"false"))
       end
else let version = (string_of_bool wab)^":wab-"^(string_of_int wbb)^"-pp:"^(string_of_bool pp) in begin
Edukio.put_data (tmpfile^"bug"^(string_of_int !cpt)^".bugeduk") tmpfile n !rc wmat pmat;
(-100000.,-1,"eduk:"^version)
end
      in 
      (tfwd,rescomputation),over in
    let tsols1=
      (List.map one 
         [(10000,true,true);
          (10000,false,true);
          (0,true,true);
          (0,false,true);
          (0,false,false);])in
    let over_flow = List.exists (fun c -> "true"=snd c) tsols1 in
    let vmt = 
      if not over_flow then
        try !fmt wmat pmat !rc n (tmpfile) with
          MT_too_long -> begin 
            fmt := (fun _ _ _ _ _ ->         (-.(10.**5.),0),"mt too long");
            (-.(10.**5.),0),"mt too long" end 
      else begin 
        fmt := (fun _ _ _ _ _ ->         (-.(10.**5.),0),"int_over_flow");
        (-.(10.**5.),0),"true"
      end
(*    (-.1.,-1),"no mt"*)
    in
    let tsols = tsols1@[vmt] in
    if Sys.file_exists tmpfile then Unix.unlink tmpfile;
    let ((t,v),_)::suite = tsols in
    List.iter (fun ((_,u),s) -> 
      if u<>v then 
        begin diff:= true;
          change_info rinfo "def"  "true" "mt too long" s
        end) suite;
    if !diff && (!rinfo = "def")
    then begin incr cpt; 
      List.iter (fun ((u,v),s) -> prerr_string (s^" ");prerr_float u;prerr_string" ";prerr_int v;prerr_string"\n";flush stderr) tsols;
(*      Edukio.put_data ("diffsolver"^tmpfile^(string_of_int !cpt)^".ukp")"" n !rc wmat pmat*)
    end;
    lr :=   List.map2 (+.) !lr (fst(List.split(fst(List.split tsols))));
  done;
  (List.map (fun i -> i /. (float nbc)) !lr, !diff, !rinfo)
;;


let nb_instances tmpfile f wmin wmax pmin pmax n nb form=
  let rt = ref [0.;0.;0.;0.;0.;0.] in
  let diff = ref false in
  let rinfo = ref "def" in
  for i = 1 to nb do
    let fmt = ref one_instance_mt in
    let l,d,ri = (Unix.handle_unix_error(one_instance (tmpfile^(string_of_int i)^(!ext)) fmt f wmin wmax pmin pmax n !rnbc) form) in
    rt := List.map2 (+.) !rt l;
    diff := !diff || d;
      change_info rinfo "def" "true" "mt too long" ri
  done;
  List.map (fun x -> x /. (float nb)) !rt, !diff, !rinfo
    
  let data n wmin wmax pmin pmax form = match form with
  | "ss2" ->   Datagen.subset n wmin wmax 1 false
  | "ss5" ->   Datagen.subset n wmin wmax 5 false
  | "unrss"|"ss" -> Datagen.unrsubset n wmin wmax 1 false
  | "sc"  | "scbis" -> Datagen.strongcor n wmin wmax pmin
  |  "saw" -> Datagen.saw n wmin wmax 10
  |  "unrsaw" -> Datagen.unr_saw n wmin wmax pmin pmax 10
  | "real" -> Datagen.nosimpledom2 n wmin wmax pmin pmax
  | "rh" -> Datagen.realhard n wmin wmax
  | "hi" -> Datagen.hardincreasingratio n wmin wmax pmin pmax
  | "hd" -> Datagen.harddecreasingratio n wmin wmax pmin pmax
  | "w" -> Datagen.weakcor n wmin wmax pmin
  | "chung" -> Datagen.chung n wmin pmin true
  | "chungavis" -> Datagen.avischung n pmin true    

let form = ref "w"
let n = ref 10
let wmin = ref 100
let wmax = ref 10000
let pmin = ref 10
let pmax = ref 100000
let step = ref 10
let nbc = ref 10
let nb = ref 100

let _ = 
begin
  Arg.parse 
    [("-form",Arg.String(fun s -> form := s),"The used formula");
      ("-ext",Arg.String(fun s -> ext := ("."^ s)),"The basis of file extension");
     ("-n",Arg.Int(fun i -> n := i),"The number of item type");
     ("-nbc",Arg.Int(fun i -> rnbc := i),"The number of different capacities");
     ("-nb",Arg.Int(fun i -> nb := i),"The number of different instances");
     ("-wmin",Arg.Int(fun i -> wmin := i),"The minimal weight");
     ("-wmax",Arg.Int(fun i -> wmax := i),"The maximal weight");
     ("-pmin",Arg.Int(fun i -> pmin := i),"The minimal profit");
     ("-pmax",Arg.Int(fun i -> pmax := i),"The maximal profit");
     ("-step",Arg.Int(fun i -> step := i),"The coefficient betwenn profit and weight");
   ]
  (fun _ -> ())
  "Vérifiez la syntaxe";
  
  let tmpfile = !ext ^"/"^ !form ^ ".tmp" in
  if !form <> "unrss" &&  !form  <> "unrsaw" &&  !form  <> "chungavis" && !form  <> "hi" && !form  <> "hd" && !form  <> "w" &&  !form  <> "sc"&&  !form  <> "scbis" && !wmax - !wmin < 2 * !n -10 then () else begin
    try
      let lres,diff,rinfo = nb_instances tmpfile data !wmin !wmax !pmin !pmax !n !nb  !form in
      Printf.printf "%d\t" !n;
      List.iter (Printf.printf "%f\t") lres;
      Printf.printf "%b\t" diff;
      Printf.printf "%s\n" (if rinfo = "true" then "overflow" else if rinfo = "def" then "" else rinfo);
      flush stdout;
    with e -> (prerr_endline (Printexc.to_string e);flush stderr; raise e)
  end
end
;;
