
(
(extension
  (attribute_id ) @ppx_name 
   (attribute_payload 
    (expression_item 
     (quoted_string 
      (quoted_string_content) @injection.content
      
      )))
 )
(#eq? @ppx_name "html")
 (#set! injection.language "html")
; we set the priority low so our interpolation can shine through
(#set! "priority" 95)
)
