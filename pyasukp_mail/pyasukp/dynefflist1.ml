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

(* $Id: dynefflist.ml,v 1.4 2005/04/27 13:09:48 poirriez Exp $ *)
(* dynefflist.ml *)

(** This module  implements ordered sequences of values, indexed
    with integers, Initially the structure [st] is an array 
    of size [k] initialized with of empty lists.
    The value associated with the integer [i] is in the slot [st.(i/(k-1))].
    This allow an access in theta(k).
*)
type 'a cell = {key : int; v : 'a}
type 'a packet = {mutable sup_a : 'a ;mutable pack: 'a array}
type 'a t =  
    { dat : 'a packet array;
      k : int; 
      d : int;
      access : 'a cell  -> int * int; 
      ord : 'a  -> 'a  -> bool}

let create k d ord init= 
{dat = Array.init k (fun i -> {sup_a= init;pack =[||]}); k = k; d = d; access  = (fun c -> c.key / d, c.key mod d); ord = ord}


let improve_sup_a ord v p =
 if ord v (p.sup_a) then false else begin p.sup_a <- v; true end

let  loc_insert_and_improve k ord j v p =
  if ord v p.sup_a then false else
  match p.pack with
  | [||] -> begin 
      let r = Array.create k 0 in
      for i = j to k-1 do r.(i) <- v done;
      p.pack <- r;
      true
  end
| r when ord r.(j) v ->
       begin
         r.(j) <- v;
         let i = ref (j+1) in
	 while !i < k && ord r.(!i) v do
	   r.(!i) <- v; incr i
	 done;
	 true
       end
| _ -> false


let rec improve_nexts st v i =
 let b = improve_sup_a st.ord v st.dat.(i) in
  if b && i < st.k -1 then
    improve_nexts st v (i+1)
 else ()
   
let insert_and_improve st c =
 let ord = st.ord in
 let i,j = st.access c in 
  let ins = loc_insert_and_improve st.d ord j c.v st.dat.(i) in
  if ins && i < st.k - 1 then improve_nexts st c.v (i+1);
  ins

let print st =
 Printf.printf "dat:\n";
 Array.iteri (fun i p -> Printf.printf "i:%d sup_a:%d\n" i p.sup_a;
   if p.pack <> [||] then begin
   Array.iteri (fun i c -> (Printf.printf "k:%d;v:%d;" i c; if i mod 10 = 0 then print_newline())) p.pack;
   Printf.printf "\n" end) st.dat;
   Printf.printf "k:%d  d:%d\n" st.k st.d



