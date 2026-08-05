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



(* $Id: chainlist.ml,v 1.2 2005/02/25 15:24:23 poirriez Exp $ *)
(*chainlist.ml*)

(**This module implements explicitly chained lists*)

type 'a elt = { info : 'a ; next : 'a rt; } 
and 'a t = End | E of 'a elt
and 'a rt = 'a t ref
exception Empty

let create i = ref (E {info = i; next = ref End})

let is_empty cl = !cl = End

let rec delete i l = match !l with
|  End -> ()
|  E e -> if e.info = i then l:= !(e.next) else delete i e.next

let iter f l=
  let tmp = ref !l in
  while !tmp <> End do
    match !tmp with
    | E e -> ignore (f e.info); 
        tmp := !(e.next)
    | End -> assert false
  done

let rec fold f init l= match !l with
| E e -> fold f (f init e.info) e.next
| End -> init

let insert_sorted ord l i=
  let notdone = ref true in
  match !l with
  |  E e  ->
      if ord e.info i then l := E {info=i; next = ref (!l) }
      else
        begin
          let rcurrent = ref e in
          while !notdone do
            let rsuiv = !rcurrent.next in
            let suiv = !rsuiv in
            match suiv with
            | End -> (!rcurrent.next := E{info =i; next = ref End}; notdone := false)
            | E e ->
                if ord e.info i then 
                  begin
                    !rcurrent.next := E{info =i; next = ref suiv }; 
                    notdone := false
                  end
                else 
                  begin
                    rcurrent := e
                  end
          done
        end
  | End -> l:= E{info =i; next = ref End}

let put_in_sorted pred i l =
  let notdone = ref true in
  match !l with
  |  E e  ->
      if pred e.info  then l := E {info=i; next = ref (!l) }
      else
        begin
          let rcurrent = ref e in
          while !notdone do
            let rsuiv = !rcurrent.next in
            let suiv = !rsuiv in
            match suiv with
            | End -> (!rcurrent.next := E{info =i; next = ref End}; notdone := false)
            | E e ->
                if pred e.info  then 
                  begin
                    !rcurrent.next := E{info =i; next = ref suiv }; 
                    notdone := false
                  end
                else 
                  begin
                    rcurrent := e
                  end
          done
        end
  | End -> l:= (E{info =i; next = ref End})

let insert_head l i = 
  let next = !l in
  l := (E{info = i; next = ref next})

let hd a = match !a with
| E e -> e.info
| End -> raise Empty

let take a = match !a with
| E e ->
    let h = e.info in
    a := !(e.next); h
| End -> raise Empty

let is_single a =
 match !a with (E e) -> !(e.next) = End | _ -> false;;

let delete_cond_all cond l =
  match !l with
  |  End -> ()
  |  E e -> begin
      let tmp = ref e in
      while !(!tmp.next) <> End do
        match !(!tmp.next) with
        | E e ->
            if cond e.info then
              !tmp.next := !(e.next)
            else
              tmp := e
        | End -> assert false
      done;
      if cond e.info then l := !(e.next)
  end
        
let length l =
  let res = ref 0 in
  iter (fun i -> incr res) l;
  !res
    
let get_equiv_heads equiv cl = match !cl with
| E e -> 
    let h = e.info and tail = e.next in
    let equivh = ref [] in
    cl := !tail;
    begin
      try
        while equiv h (hd cl) do 
          let suiv = take cl in
          equivh := suiv :: !equivh
        done;
        (h, !equivh)
      with Empty -> (h, List.rev !equivh)
    end
| End -> raise Empty
      
let rec tail_after pred cl = match !cl with
| E e when pred e.info ->  cl
| E e -> tail_after pred e.next
| End -> cl
      
let rec find pred cl = 
  match !cl with
  | E e when pred e.info ->  e.info
  | E e -> find pred e.next
  | End -> raise Not_found
        
