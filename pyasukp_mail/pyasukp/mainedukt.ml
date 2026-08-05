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

(* $Id: mainedukt.ml,v 1.6 2005/04/27 13:09:48 poirriez Exp $ *)

open Arg
open Globals
open Eduk
open Wandp.M
open Parsecl
open Init
open Sequences
open Datastruct
open Datastructtype

(** The entry point, uses values as defined in the command line
    via the options parsed by [Parsecl.parse]
*)
let run() =
  Gc.set {(Gc.get()) with Gc.space_overhead = 190};
  if !pversion then begin 
    Printf.printf "Version is: %s\n" Version.release;
    exit 0
  end;
  let ftiming f v = 
    if !timing then Timing.timing f v else (-1.,f v)
  in
  if !solveks then
    begin
      if !trace then print_variables ();
      let compute =  Eduk.forward in 
      let tfwd, rescomputation =
        ftiming  (fun () -> compute !rw !rp !rc !with_bounds !with_all_bounds) ()
      in
      if !trace then begin Seq.print_seq sequence_result end;
      let wl = if Seq.isempty sequence_result then zerow else 
      let (wl,_,_) =Seq.lastval sequence_result in wl
      in
      let chout = 
        if !rr = "" then stdout 
        else  open_out_gen  [Open_wronly;Open_append;Open_creat] 0o666  !rr
      in 
      (match !(bound) with Some b -> if Bounds.sort_of b = BOUNDS.Sv then (output_string chout "Saw ukp ";flush stdout)
      else (output_string chout "Not a Saw ukp ";flush stdout);
      | _ -> (output_string chout "no bound ";flush stdout));
      if !pr_col_und_it then begin
        output_string chout "Indexes of the undominated items (collective dominance):\n";
        (Queue.iter (Printf.fprintf chout "%8d;") required);
        flush chout;
        output_string chout "\n";
      end;
      let nb_critical_points_in_sequence_result= Seq.length sequence_result
      in
      let nbu = Queue.length required in
      let remain_thresh_undom = 
        Chainlist.length decreasingS in
      let rtback = ref 0. in
      let tback,(sol) =
        ftiming 
          (fun () -> Eduk.rebuildsol !rw !rp !rc rescomputation) ()
      in
      let (wopt,popt) = Eduk.accumulate !rw !rp sol in
      rtback := tback;
      let pl,lastc,wreduct,best = match rescomputation with 
      | Normal(pl,_,b,_,_,lastc,wred) -> pl,lastc,wred,b 
      | _ -> false,wl,zerow,(0,0)
      in
      if not !batch then
        begin
          output_string chout ("#The Result by PYAsUKP release: "^(Version.release)^"\n");
          output_string chout 
            ("#For datas: "^
             (if !rs = "" then ("generated with formula "^ !name ^" ;" ) else " File: "^ !rs)^
             "\n");
          output_string chout 
            ("#The optimal value for the given capacity\n"^
             (string_of_profit popt)^"\n"^
             "#The minimal capacity  for the optimal value\n" ^
             (string_of_weight wopt)^
             (match !bound with Some bound -> 
               "\nUpper bound: "^ 
               (string_of_profit (bound.BOUNDS.u)^
                (match rescomputation with With_bound( _) ->
                  "\nTHE BOUND STOPPED THE COMPUTATION" | _ -> ""))
             | None -> "")^
             "\n\nPeriod reached: "^(string_of_bool pl)^
             (if not pl then 
               " ; Remaining  undominated items: "^(string_of_int remain_thresh_undom) 
             else "")^
             " ; Not collectively dominated items: "^(string_of_int nbu)^
             " ;\n Last capacity calculated: "^ (string_of_weight lastc)^
             " ;\n reduction capacity : "^ (string_of_weight wreduct)^
             " ;\n nb bb nodes : "^ (string_of_int !nbnode)^
             "\n\n"
            );
          output_string chout "\t index \t nb \t w \t p\n";
          output_string chout 
            (List.fold_left 
               (fun s (i,r) ->
                 if r>0 then 
                   s^"\t"^(string_of_int i)^"\t"^
                   (string_of_int r)^"\t"^
                   (Wandp.M.string_of_weight !rw.(i))^"\t"^
                   (Wandp.M.string_of_profit !rp.(i))^"\n"
                 else s
               )
               "sol: \n" sol);
          flush chout;
          output_string chout ("\nBest_Item: "^(string_of_int (Astore.M.get iteminfos.item best))^" \n");
          flush chout;
          output_string chout "\nTotal Time : ";
          Printf.fprintf chout "%10.5f\n" (tfwd +. !rtback); 
          output_string chout "\n\nTime forward : ";
          Printf.fprintf chout "%10.5f\n" tfwd; 
          output_string chout "Time backtrack : ";
          Printf.fprintf chout "%10.5f\n" !rtback; 
          output_string chout "Nb critical points in sequence_result: ";
          Printf.fprintf chout "%20d\n"  nb_critical_points_in_sequence_result; 
          flush chout;
          if !rr <> "" then close_out chout
        end
      else
        begin
          Printf.fprintf chout "%10d\t%b\t%10d\t%10d\t%b\t%15s\t%10.5f\t%d\n"
            nbu pl remain_thresh_undom nb_critical_points_in_sequence_result  (not !messbigint) (string_of_profit popt) 
            (tfwd +. tback) !nbnode; 
          flush chout;
          if !rr <> "" then close_out chout
        end
    end
      
      
      

let _ = Printexc.print parse (); Printexc.print run ()
;;
