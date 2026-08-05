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



(* $Id: sequences.ml,v 1.2 2005/02/28 10:39:35 poirriez Exp $ *)
(*seq.ml*)
(*All the functions are unsafe, they assume they are used with correct 
 arguments
*)

module type SEQ=
sig
type elt1 = Wandp.M.w
type elt2 = Wandp.M.p
type elt3 = int * int
type elt = elt1 * elt2 * elt3
type index = int * int
type t

val zero_index: index (* the starting point *)
val length : t -> int
val sizemax2 : t ->  int
    (*[sizemax2 ] is the size of the sub-array if fifo is implemented
      with arrays of arrays. it returns -1 if it is not the case*)
val create : int -> int -> t
	(*[create n m] return a new empty fifo of initial size upper bound n * m *)
val reset : t -> unit
	(*[reset a] empty the fifo a*)
val resetout : t -> unit
	(*[resetout a] forgive that data were got out of the fifo a*)
val put : t -> elt  -> unit
	(*[put a (w,p)] put the couple [(w,p)] at the end of [a]*) 
val put_replace : t -> elt -> unit
	(*[put_replace a (w,p)] put the couple [(w,p)] at the end of [a], replacing the previous last item *) 
val get : t -> elt 
	(*[get a]  returns the current element in fifo [a] and moves the 
          read pointer to the next one, elements are not removed*)
val getdown : t -> elt
	(*[getdown a]  returns the current element in fifo [a] and moves the 
          read pointer to the prev one, elements are not removed*)
val peek : t -> elt 
	(*[peek a] returns the first element of the fifo [a] without
          moving the read pointer.*)
val peek1 : t -> elt1
	(*[peek1 a] returns the first component of the first 
          element of the fifo [a] without  moving the read pointer.*)
val junk : t -> unit
	(*[junk a] moves the read pointer to the next element of the fifo [a]*)
val lastval : t -> elt
        (*[lastval a] returns the last value entered in [a]*)
val lastin : t -> index
	(*[lastin a] returns the index of the last value entered in [a]*)
val jumpin : t -> index -> unit
	(*[jumpin a n] move the in pointer to the nth value entered in [a]*)
val jumpout : t -> index -> unit
	(*[jumpout a n] move the out pointer to the nth value entered in [a]*)
val iter : (elt1 -> elt2 -> elt3 -> 'a) -> t -> unit
	(*[iter f a] evaluates [f w p k] in turn for all elements [(w,p,k)]
          of [a], from the least recently entered to the most recently
          entered. The fifo itself is unchanged
        *)
val isempty : t -> bool
	(*[isempty a] is true iff a isempty*)

val item_in : t -> index
val item_out : t -> index

val  print : (elt1 -> unit) -> (elt2 -> unit) -> (elt3 -> unit) -> t -> unit
val  print_seq :  t -> unit
val  print_seq_from :  t -> int * int -> unit
       (* to print a sequence *)
val search_down1_from : (elt1 -> bool) -> t -> index -> (elt * index)
val search_down1 : (elt1 -> bool) -> t -> (elt * index)
    (*[search_down1 pred s]  returns the fisrt downstairs  value [(e1,e2)] together with its index such that
      [pred e1 == true] assuming that [pred] is a decreasing function.*)
    val search_max_two_ways :
	t-> Wandp.M.w -> (elt * index ) -> elt -> index * index * Wandp.M.p
end 

module Seq : SEQ = struct
type elt1 = Wandp.M.w
type elt2 = Wandp.M.p
type elt3 = int * int
type elt = elt1 * elt2 * elt3
type index = int*int
type t = {mutable tabw: elt1 array array;
          mutable tabp: elt2 array array;
          mutable tabk: elt3 array array;
          mutable cin: int;
          mutable cout: int;
          mutable sizemax1 : int;
          sizemax2 : int;
          mutable tabin : int;
          mutable tabout : int}

let sizemax2 a = a.sizemax2
let zero_index = 0,0      
let create n m = 
  let res =
    {tabw = Array.create n [||];
     tabp = Array.create n [||];
     tabk = Array.create n [||];
     cin = 0;
     cout = 0;
     tabin = 0;
     tabout = 0;
     sizemax1 = n-1;
     sizemax2 = m-1}
  in
  res.tabw.(0) <- Array.create m 0;
  res.tabp.(0) <- Array.create m 0;
  res.tabk.(0) <- Array.create m (0,0);
  res
    
let reset a = a.cin <-0; a.cout <- 0;a.tabin <-0; a.tabout <- 0
let resetout a =  a.cout <- 0; a.tabout <- 0

let next_place_up a (j,i) =
 if i = a.sizemax2 then (j+1,0) else (j,i+1)

let next_place_down a (j,i) =
 if i = 0 then (j-1,a.sizemax2) else (j,i-1)

let put a (w,p,k) =
  let i = a.tabin in
  a.tabw.(i).(a.cin)<-w;
  a.tabp.(i).(a.cin)<-p;
  a.tabk.(i).(a.cin)<-k;
  if a.cin < a.sizemax2 then   a.cin <- a.cin + 1
  else 
    begin
      if a.tabin >= a.sizemax1 then
        begin
          let newsize = a.sizemax1+1000 in
          let ntabw = Array.create (newsize+1) [||]
          and ntabp = Array.create (newsize+1) [||]
          and ntabk = Array.create (newsize+1) [||]
          in
          for i = 0 to a.sizemax1  do
            Array.unsafe_set ntabw i (Array.unsafe_get a.tabw i);
            Array.unsafe_set ntabp i (Array.unsafe_get a.tabp i);
            Array.unsafe_set ntabk i (Array.unsafe_get a.tabk i)
          done;
          a.tabw <- ntabw;
          a.tabp <- ntabp;
          a.tabk <- ntabk;
          a.sizemax1 <- newsize
        end;
      a.tabin <- a.tabin +1;
      a.cin <- 0;
      let i = a.tabin in
      a.tabw.(i) <- Array.create (a.sizemax2 +1) 0;
      a.tabp.(i) <- Array.create (a.sizemax2 +1) 0;
      a.tabk.(i) <- Array.create (a.sizemax2 +1) (0,0);
    end
      
let get a =
  let i = a.tabout in
  let w = a.tabw.(i).(a.cout) 
  and p = a.tabp.(i).(a.cout) 
  and k = a.tabk.(i).(a.cout) 
  in
  if a.cout < a.sizemax2 then  a.cout <- a.cout+1
  else 
    begin
      a.tabout <- a.tabout+1;
      a.cout <- 0
    end;
  (w,p,k)
    
let getdown a = 
  let i = a.tabout in
  let w = a.tabw.(i).(a.cout) 
  and p = a.tabp.(i).(a.cout) 
  and k = a.tabk.(i).(a.cout) 
  in
  if a.cout >0 then  a.cout <- a.cout-1
  else 
    begin
      a.tabout <- a.tabout-1;
      a.cout <- a.sizemax2
    end;
  (w,p,k)
    
let peek a = let j = a.tabout in a.tabw.(j).(a.cout),a.tabp.(j).(a.cout),a.tabk.(j).(a.cout)
  
let peek1 a = let j = a.tabout in a.tabw.(j).(a.cout)
  
let junk a =
  if a.cout < a.sizemax2 then a.cout <- a.cout+1 
  else (a.tabout <- a.tabout +1;a.cout <-0)
      
let lastin a =  if a.cin = 0 then a.tabin -1, a.sizemax2 else a.tabin, a.cin -1
  
let lastval a = let j,i = lastin a in a.tabw.(j).(i), a.tabp.(j).(i), a.tabk.(j).(i)
  
let put_replace a (w,p,k) =
  let i,j = lastin a in
  a.tabw.(i).(j) <- w;
  a.tabp.(i).(j) <- p;
  a.tabk.(i).(j) <- k
      
let jumpin a (i,n) = a.cin <- n; a.tabin <- i
    
let jumpout a (i,n) = a.cout <- n; a.tabout <- i
    
let isempty a = a.cin = a.cout & a.tabin = a.tabout
    
let item_in a =  a.tabin,a.cin
    
let item_lastin  a = 
  if a.cin = 0 then a.tabin -1, a.sizemax2 else a.tabin, a.cin-1
    
let item_out a = a.tabout,a.cout
    
let incr_read a comp =
  let rj = ref 0 and ri = ref 0 and nj = ref 0 and ni = ref 0 in
  let rec trav w =
    if (!ni = a.tabin && !nj = a.cin) || comp w a.tabw.(!ni).(!nj) then
      (a.tabw.(!ri).(!rj),a.tabp.(!ri).(!rj),a.tabk.(!ri).(!rj))
    else begin
      ri := !ni; rj := !nj; incr nj;
      if !nj > a.sizemax2 then begin
        incr ni; nj := 0
      end;
      trav w
    end
  in trav
    
let iter_from f a (j,i) =
  let rj = ref j and ri = ref i in
  while !rj <= a.tabin do
    while (!ri <= a.sizemax2 & !rj < a.tabin) or (!ri < a.cin ) do
      f  a.tabw.(!rj).(!ri) a.tabp.(!rj).(!ri) a.tabk.(!rj).(!ri);
      incr ri
    done;
    ri := 0;
    incr rj
  done
    
let iter f a = (*let rj = ref a.tabout and ri = ref a.cout in*)
  iter_from f a (0,0)
    
let print_from print1 print2 print3 s (i,j)=
  iter_from (fun eltw eltp eltk -> Format.print_string "\n("; print1 eltw;
    Format.print_string ","; print2 eltp;Format.print_string ","; print3 eltk;
    Format.print_string ")") s (i,j);
  Format.print_string "\n"
let print p1 p2 p3 s = print_from p1 p2 p3 s (0,0)
    
let print_seq_from s (i,j) =
  print_from
    (fun i -> Format.print_string (Wandp.M.string_of_weight i))
    (fun i -> Format.print_string (Wandp.M.string_of_profit i))
    (fun (i,j) -> Format.print_string ("("^(string_of_int i)^","^(string_of_int j)^")")) s (i,j)

let print_seq s = print_seq_from s (0,0)
    
(* We assume that [pred] is a decreasing function. 
   This means that if [pred w p == true ] then for all w' p' before (below)  w p in the sequence,
   [pred w' p' == true].*)
    
let search_down1 pred a =
  let j,i = lastin a in
  let lastvalw = a.tabw.(j).(i) in
  if pred lastvalw then ((lastvalw,a.tabp.(j).(i), a.tabk.(j).(i) ),(j,i)) else
  let startj = ref j in
  while not (pred a.tabw.(!startj).(0)) do  if !startj = 0 then raise Not_found else decr startj done;
  let starti = if !startj = j then ref i else ref a.sizemax2 in
  while not (pred a.tabw.(!startj).(!starti)) do  decr starti done;
  let j,i = !startj, !starti in ((a.tabw.(j).(i), a.tabp.(j).(i), a.tabk.(j).(i)),(j,i))
    
let search_down1_from pred a (j,i) =
  let lastvalw = a.tabw.(j).(i) in
  if pred lastvalw then ((lastvalw, a.tabp.(j).(i), a.tabk.(j).(i)),(j,i)) else
  let startj = ref j in
  while not (pred a.tabw.(!startj).(0)) do  if !startj = 0 then raise Not_found else decr startj done;
  let starti = if !startj = j then ref i else ref a.sizemax2 in
  while not (pred a.tabw.(!startj).(!starti)) do  decr starti done;
  let j,i = !startj, !starti in ((a.tabw.(j).(i), a.tabp.(j).(i), a.tabk.(j).(i)),(j,i))

let search_last_up_between pred a (j,i) (jl,il) =
 let starti = ref i and startj = ref j in
  while (!starti <> il || !startj<>jl) && (pred a.tabw.(!startj).(!starti)) do 
    let j',i' = next_place_up a (!startj,!starti) in
    starti := i' ;  startj := j'
  done;
  let j = !startj and i = !starti in
  ((a.tabw.(j).(i), a.tabp.(j).(i), a.tabk.(j).(i)),(j,i))

let search_max_two_ways a c ((w1,p1,_),((j1,i1) as ji1)) (wl,_,_) =
 let rec search ((_,_,pmax) as r) ((j0,i0) as ji0) ((j1,i1) as ji1) =
   let p0 = a.tabp.(j0).(i0)
   and p1 = a.tabp.(j1).(i1) in
   let p' = p0 + p1 in
   let w1 = a.tabw.(j1).(i1) in
   let newr = if p' > pmax then (ji0,ji1,p') else r in   
   if w1 >= wl then newr else
   let (j1',i1') as ji1' = next_place_up a (j1,i1) in
   let w1' = a.tabw.(j1').(i1')  in
   let fit = ( >= ) (c - w1') in
   let (_,((j0',i0') as ji0'))= search_down1_from fit a (j0,i0)  in
   search newr ji0' ji1'
 in 
 let (_,ji0) = search_down1_from (( >= ) ( c - w1))  a ji1 in
 search (ji0,ji1,p1) ji0 ji1

     
let length a = 
  let j,i = lastin a in j * a.sizemax2 + i + 1
    
end

