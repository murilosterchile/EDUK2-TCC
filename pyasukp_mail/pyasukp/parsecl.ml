(***************************************************************************)
(*                                  PYAsUKP                                *)
(*    PYAsUKP: Yet Another solver (for the) Unbounded Knapsack Problem     *)
(*           Vincent Poirriez with Nicola Yanev and Rumen Andonov          *)
(*                          LAMIH-ROI UMR CNRS 8530                        *)
(*  Copyright 1998-2005  Vincent Poirriez				   *)
(* <vincent Dot poirriez At univ-valenciennes Dot fr>                      *)
(*  This file is part of PYAsUKP.                                          *)
(*									   *)
(*  PYAsUKP is free software; you can redistribute it and/or modify        *)
(*  it under the terms of the GNU General Public License as published by   *)
(*    the Free Software Foundation; either version 2 of the License, or	   *)
(*    (at your option) any later version.				   *)
(*    PYAsUKP is distributed in the hope that it will be useful,	   *)
(*    but WITHOUT ANY WARRANTY; without even the implied warranty of	   *)
(*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the	   *)
(*    GNU General Public License for more details.			   *)
(*									   *)
(*  You should have received a copy of the GNU General Public License	   *)
(*  along with Foobar; if not, write to the Free Software		   *)
(*  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA*)
(***************************************************************************)

(* $Id: parsecl.ml,v 1.10 2005/05/04 16:31:20 poirriez Exp $*)
(*parsecl.ml*)

(* Here are the tools to parse the command ligne of the executables*)
open Arg
open Globals
open Wandp.M


let pr_col_und_it = ref false
and batch = ref false
and pversion = ref false

(** put true if when you are timing, you want only the total time *)
and onlytt = ref false

(** put false if  you don't want a timing *)
and timing = ref true


(*
  [source (rm,rc,robjs,rs) s] read data in the file named [s] and return them
  in the four references.
*)
let source (rn,rc,rw,rp,rs) s =
 rs := s;
 let w,p,n,c = Edukio.items_of_file s in 
 rn := n; rc := c; rw := w; rp := p;
;;
(*
  [ save (rm,rc,rw,rp,wmin,wmax,pmin,pmax,name,interest,step,seed) rf s ] save
  the generated data in a file named [s]. It adds a comment which recall the
  used formula.
*)
let save (rn, rc, rw, rp, wmin, wmax, pmin, pmax, name,interest,step,seed) rf s = 
 rf:=(fun () -> Edukio.put_data s 
                ("EDUK source data file build with formula: " ^ !name^
                 "; seed: "^(string_of_int !seed) ^
                 "; wmin: "^ (Wandp.M.string_of_weight !wmin)^
                 "; wmax: "^(Wandp.M.string_of_weight !wmax)^ 
                 ("; pmin: "^(Wandp.M.string_of_profit !pmin))^ 
                 ("; pmax: "^(Wandp.M.string_of_profit !pmax))^ 
                 "; c: "^(string_of_weight !rc)^ 
                 "; n: "^(string_of_int !rn)^
                 ("; interest: "^(string_of_int !interest))^ 
                 ("; step: "^(Wandp.M.string_of_profit !step)))
                 !rn !rc !rw !rp);;

(*
  To choose the correct formula
*)
 let formule name rn rc rw rp pmin pmax wmin wmax step interest ns s=
   match s with
   | "s" | "S" |"Strong" -> let w,p=Datagen.strongcor !rn !wmin !wmax !step in 
     (rw:=w;rp:=p;name := "strong correlation")
   | "chung" | "Chung" | "c" | "C" -> let w,p=Datagen.chung !rn !wmin  !step !ns in 
     (rw:=w;rp:=p; name := "Chung formula")
   | "avis" -> let w,p = Datagen.avissubset !rn !ns in
     let n = !rn in
     let c = n*(n+1)*((n-1)/2)+n*((n-1)/2) in if !rc = None then rc := Some c else ();
          (rw:=w;rp:=p;  name := "AVIS formula")
   | "chungavis" -> let w,p = Datagen.avischung !rn !step !ns in
     let n = !rn in
     let c = n*(n+1)*((n-1)/2)+n*((n-1)/2) in if !rc = None then rc := Some c else ();
          (rw:=w;rp:=p;  name := "CHUNG AVIS formula")
   |"w" | "W" |"weak" | "Weak"-> 
       let w,p=Datagen.weakcor !rn !wmin !wmax !step in 
       (rw:=w;rp:=p; name:= "weak correlation")
   |"rc" | "RC" -> 
       let w,p=Datagen.randcor !rn !wmin !wmax !step in 
       (rw:=w;rp:=p; name:= "random correlation")
   |"n" | "N"| "not" | "Not" -> 
       let w,p=Datagen.notcor !rn !wmin !wmax !pmin !pmax in 
       (rw:=w;rp:=p; name := "not correlated")
   |"u" | "U" -> 
       let w,p=Datagen.nosimpledom !rn !wmin !wmax !pmin !pmax !ns in 
       (rw:=w;rp:=p; name := "no simple dominance")
   |"nsd" | "NSD" -> 
       let w,p=Datagen.nosimpledom2 !rn !wmin !wmax !pmin !pmax !ns in 
       (rw:=w;rp:=p; name := "no simple dominance")
   |"nsds" | "NSDS" -> 
       let w,p=Datagen.nosimpledomstep !rn !wmin !wmax !pmin !pmax !ns !step in 
       (rw:=w;rp:=p; name := "no simple dominance step")
   |"nsds2" | "NSDS2" -> 
       let w,p=Datagen.nosimpledomstep2 !rn !wmin !wmax !pmin !pmax !ns !step in 
       (rw:=w;rp:=p; name := "no simple dominance step with pi>wi")
   |"prm" | "PRM" -> 
       let w,p=Datagen.primsw !rn !wmin !wmax !pmin !pmax !ns !step in 
       (rw:=w;rp:=p; name := "no simple dominance and no weight divides another weight")
   |"rh" | "Rh"| "RH"-> 
       let w,p=Datagen.realhard !rn !wmin !wmax  in (rw:=w;rp:=p)
   |"hd" | "Hd"| "HD"-> 
       let w,p=Datagen.harddecreasingratio !rn !wmin !wmax !pmin !pmax in (rw:=w;rp:=p)
   |"hi" | "Hi"| "HI"-> 
       let w,p=Datagen.hardincreasingratio !rn !wmin !wmax !pmin !pmax in (rw:=w;rp:=p)
   |"Saw" | "saw" -> 
       let w,p=Datagen.saw !rn !wmin !wmax !step  in (rw:=w;rp:=p)
   |"UnrSaw" | "unrsaw" -> 
       let w,p=Datagen.unr_saw !rn !wmin !wmax !pmin !pmax !step  in (rw:=w;rp:=p)
   |"subset" |"ss" -> 
       let w,p = Datagen.subset !rn !wmin !wmax !interest false in 
       (rw := w;rp:=p;  name := "subset sum problem like")
   | "sortss" |"subsetsort" |"sss" ->
       let w,p = Datagen.subset !rn !wmin !wmax !interest true in 
       (rw := w;rp:=p; name:= "sorted subset sum problem")
   |"unrsubset" |"unrss" -> 
       let w,p = Datagen.unrsubset !rn !wmin !wmax !interest false in 
       (rw := w;rp:=p;  name := "subset sum problem like")
   | "sortunrss" |"unrsubsetsort" |"unrsss" ->
       let w,p = Datagen.unrsubset !rn !wmin !wmax !interest true in 
       (rw := w;rp:=p; name:= "sorted subset sum problem")
   |"ls" |"yap" ->
       let w,p,c = Datagen.yapnosimpledom !rn !wmin !wmax !pmin !pmax !ns in 
       (rw := w;rp:=p; rc := Some c; name:= "yap problem")
   |_ -> failwith ("eduk does not know what to with -form "^s^"  ")
         
    
let parse ()=
  Arg.parse 
    [("-src",String (source (rn,rc,rw,rp,rs)), "The source of datas");
     ("-save",
      String (save (rn,rc,rw,rp,wmin,wmax,pmin,pmax,arg_form,interest,step,seed) rf),
      "The file name to save datas") ;
     ("-res",String (fun s -> rr:=s),"The file name to save the result") ;
     ("-form", String (fun s -> arg_form := s), "The used formula to generate datas");
     ("-cap",
      String (fun i -> tmprc := Some(weight_of_string i)),"The capacity of the sack");
     ( "-brc", Int (fun i -> brc := i), "The upper bound of the capacity,which will be also above wmax");
     ("-n", Int (fun i -> rn := i), "The number of items") ;
     ("-m", Int (fun i -> rn := i), "The number of items") ;
     ( "-step", String (fun i -> step :=  profit_of_string i),
       "The step between weight and profits for correlated datas"); 
     ( "-inter", Int (fun i -> interest:= i),
       "The ratio between  profits and weight for subset datas"); 
     ( "-wmin", String (fun i -> wmin := weight_of_string i), "The minimum weight"); 
     ( "-wmax", String (fun i -> wmax := weight_of_string i), "The maximum weight"); 
     ( "-pmin", String (fun i -> pmin := profit_of_string i), "The minimum profit"); 
     ( "-pmax", String (fun i -> pmax := profit_of_string i), "The maximum profit"); 
     ( "-ccap", String (fun i -> coefcap := profit_of_string i), "The coefficient to divide the weights sum to obtain the cap"); 
     ( "-seed", Int (fun i -> seed := i), "The seed used to generate data");
     ( "-sort", Clear ns, "Precise to sort in increasing order of weight, appliable only for formulae chung, avis and chungavis");
     ( "-sizeseq", Int (fun i -> initial_size_of_sequence_result := i),
       "The initial size of the result sequence.");
     ( "-subsize", Int(fun i ->   subsize_of_sequence_result:= i),
       "The size of the sub-arrays of the result sequence");
     ( "-nosolve", Clear solveks, "To precise to  not solve to be used with -save");
     ( "-nobb", Unit (fun () -> with_bounds := false; with_all_bounds := false), "Use only dynamic programming, the default is to use DP with branch and bound");
     ( "-tr", Set trace, "To print some trace ");
     ( "-mt", Set mt, "To force the use of MT upper bound ");
     ( "-uv", Set uv, "To force the use of Uv upper bound ");
     ( "-both", Set both, "To force the use of Uv upper bound ");
     ( "-no_dominance_in_context", Clear with_all_bounds, "To avoid to check for each optimal critical point if it have no chance to be used to build the optimal knapsack with the given capacity. \nBeware, this break the assumption that at the end of the computation, the optimal solutions for all the capacity below c are known.");
     ( "-nopp", Clear prepro, "To avoid the preprocessing phase which eliminates first all the item types dominated in the context of the capacity ");
     ( "-nzh", Clear zhbr, "To specify you don't want to compute reduction with the Zhu Broughan relation");
     ( "-dm", Set dm, "To specify you want to compute reduction with the multiple dominance relation");
     ( "-pcui", Set pr_col_und_it, 
       "To print the indexes of the undominated items for collective dominance");
     ( "-hs", Int (fun i -> layer_height := i),
       "The height of the slice");
     ( "-nbs", Int (fun i -> nb2select := i),
       "The number of item types selected in each slice");
     ( "-bblim", Int (fun i -> nbmaxsol := i), "The maximum number of solutions built in the branch and bound process, if -1 then no limitation");
     ( "-bbnewv", Set bbnewv, "Uses the new version of b&b");
     ( "-bbnewv2",Unit (fun () -> bbnewv2 := true) , "Uses the new version of b&b with mult dom");
     ( "-nbb", Int (fun i -> nbbests := i), "The number of item types to be used in the branch and bound process, if -1 then max of 100 and n/100 this is the default");
     ( "-d", Int (fun i -> d := i), "greater it is, smaller is st");
     ( "-batch", Set batch, 
       "To print the values with no comment");
     ( "-onlytt", Set onlytt, 
       "To print only the total time");
     ( "-notiming", Clear timing, 
       "To avoid timing");
     ( "-v", Set pversion, 
       "To print the version");
   ]
    (source (rn,rc,rw,rp,rs)) ("Usage of"^Sys.argv.(0)^":");
  (match !rs with 
  | "" ->
      Random.init !seed;
      if Wandp.M.equal_weights Wandp.M.zerow !wmax then wmax :=   (Wandp.M.add_int_weight (10 * !rn)  !wmin);
      if Wandp.M.equal_weights Wandp.M.zerop !pmax then pmax :=   (Wandp.M.add_int_profit (100 * !rn) !pmin);
      formule name rn tmprc rw rp pmin pmax wmin wmax step interest ns !arg_form;
      if not !batch then print_endline "End of generation";
      flush stdout
  | _ -> ());
  (match !tmprc with 
  | None -> 
	if !rs = "" then 
	  if !brc = 0 then
            begin if !rc = Wandp.M.zerow then 
              let nbm = Wandp.M.quotient_profit(Wandp.M.profit_of_int max_int) !pmax in
              let seuil = Wandp.M.weight_smallereq !wmin (Wandp.M.weight_of_int (max_int/nbm))  in
              
              
	      let sw = if seuil then 
		Wandp.M.min_weight 
		  (Wandp.M.add_weights 
		     !wmax 
		     (Wandp.M.rand_weight (Wandp.M.substract_weight (Wandp.M.weight_of_int (max_int -100)) !wmax))
		  )
		  (Wandp.M.mult_int_weight nbm !wmin)
              else  Array.fold_right Wandp.M.add_weights !rw Wandp.M.zerow in
              rc := Wandp.M.div_weight_int sw !coefcap
	    end
	  else rc := (Wandp.M.add_weights 
			!wmax 
			(Wandp.M.rand_weight (Wandp.M.substract_weight (Wandp.M.weight_of_int !brc) !wmax))
		  )
  | Some i -> rc := i);
  !rf ();;

