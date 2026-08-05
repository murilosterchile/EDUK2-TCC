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

(* $Id: dynefflist.ml,v 1.6 2005/05/04 19:30:48 poirriez Exp $ *)
(* dynefflist.ml *)

(** This module  implements ordered sequences of values, indexed
    with integers, Initially the structure [st] is an array 
    of size [k] initialized with of empty lists.
    The value associated with the integer [i] is in the slot [st.(i/(k-1))].
    This allow an access in theta(k).
*)

type 'a cell = {key : int; v : 'a}
type 'a t =  
    { dat : 'a cell list array;
      k : int; 
      d : int;
      access : 'a cell -> int; 
      ord : 'a  -> 'a  -> bool}

let create k d ord= 
{dat = Array.create k []; k = k; d = d; access  = (fun c -> c.key / d); ord = ord}

let improve_hd ord c l cont = 
let b,nc,oc,nl =
match l with
| [] -> true,c,c,[c]
| c' :: s when ord c.v c'.v -> if c.key < c'.key  then true,c,c',c::l else false,c',c',l
| c' :: s -> true,c,c', c::s
in begin
(*Printf.printf "b:%B; c: %d nc: %d; oc: %d \n" b nc.key nc.v oc.v;
flush stdout;*)
b,nl
end


let rec loc_insert_and_improve ord c l = match l with
| [] -> true, [c]
| c' :: suite when c'.key < c.key ->
     if ord c'.v c.v then 
       let ins, nsuite = 
	 loc_insert_and_improve ord c suite 
       in
       if ins then ins, c'::nsuite else ins,l
     else false, l
| c' :: suite when c'.key = c.key ->

     if ord c'.v c.v then 
       true, c::suite
     else if c'.v = c.v then true,l
     else false, l
| _ -> true, c::l

let rec improve_nexts st c i =
 let b,nl = improve_hd st.ord c st.dat.(i) true in
  if b && i < st.k -1 then begin
    st.dat.(i) <- nl;
    improve_nexts st c (i+1)
  end
 else ()
   
let rec prev_known t i =
if i < 0 then 0 else
 match t.(i) with
 |[] ->  prev_known t (i-1)
 | c::_ -> c.v

let insert_and_improve st c =
 let ord = st.ord in
 let i = st.access c in 
 let l = st.dat.(i) in
 let ins =
   match l with
   |[] -> let v = prev_known st.dat (i-1) in
     if ord v c.v then begin
       st.dat.(i) <- [c]; true
     end
     else false
   | _ -> begin
       let ins,nl = loc_insert_and_improve ord c st.dat.(i) in
       st.dat.(i) <- nl;
       ins
   end
 in
 (if ins && i < st.k - 1 then 
   let d = st.d in
   let ok,nl = improve_hd st.ord {c with key = d*(1+ (c.key/d))} st.dat.(i+1) true in
   st.dat.(i+1) <- nl);
 ins

let print st =
 Printf.printf "dat:\n";
 Array.iter (fun l -> if l <> [] then begin
   List.iter (fun c -> Printf.printf "k:%d;v:%d;" c.key c.v) l;
   Printf.printf "\n" end) st.dat;
   Printf.printf "k:%d  d:%d\n" st.k st.d



