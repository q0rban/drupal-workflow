<?php

// $Id:$

/**
 * @file
 * Workflow OO structure and accompanying API.
 */
 
/**
* Workflow State class
*/
class workflow_state {
  
  var $sid;
  var $name;
  var $label;
  
  var $weight;
  var $sysid;
  var $status;
  
}

/**
* Workflow Transition class
*/
class workflow_transition {
  
  var $tid;
  var $state_name;
  var $target_state_name;  
  var $roles;
  
}


/**
* Workflow class
*/
class workflow {
  
  var $wid;
  var $name;
  var $label;
  
  var $roles;
  var $states;
  var $node_types;
  var $options;
  
  var $disabled = FALSE;
  
  function __construct($name = NULL) {
    
    // Pre-populate the object if name was passed to the constructor
    if (!empty($name)) {
      $this->name = $name;
      $obj_prop = self::query_workflow($name);
      $this->wid = $obj_prop->wid;
      $this->label = $obj_prop->label;
      $this->options = $obj_prop->options;      
      
      $this->tab_roles = $obj_prop->tab_roles;      
      $this->roles = self::tab_roles_to_array($this->tab_roles);
      
      $this->states = self::query_states($name);
      $this->node_types = self::query_nodetypes($name);
    }
    
  }

  static function tab_roles_to_array($tab_roles) {
      // If no roles are set yet, return empty array.
      if(empty($tab_roles)) {
        return array();
      }
      // If it's a single value return a single-value array.
      else if(strpos($tab_roles, ",") === FALSE) {
        return array($tab_roles);
      }
      // If none of the above, we should have a comma-separated list
      else {
        return explode(",", $tab_roles);
      }      
  }
  
  /**
  * Query database for the states associated with the current workflow 
  *
  * @param $workflow_name
  *   The machine name of the workflow
  *
  * @return
  *   The array of state objects from the database.
  */
  static function query_states($workflow_name) {    
    if (empty($workflow_name)) {
      return array();
    }
    
    
    if (!empty(self::$states_cache[$workflow_name])) {
      return self::$states_cache[$workflow_name];
    }

    
    $result = db_query("SELECT * FROM {workflow_states} WHERE workflow_name = '%s'", $workflow_name);
    
    $states = array();
    while ($row = db_fetch_typed_object($result, 'workflow_state')) {
      $states[] = $row;
    }

    self::$states_cache[$workflow_name] = $states;    
    
    return $states;
  }
  static $states_cache = array();   

  /**
  * Query database for the transitions associated with a list of states
  * (there's no direct association of transitions and a workflow in the module's data model) 
  *
  * @param $states
  *   array of state objects or state names
  *
  * @return
  *   The array of transitions, where each transition had one of the given states in the start or destination.
  */  
  static function query_transitions($states) {
    
    if (!is_array($states) || sizeof($states) < 1 ) {
      return array();
    }
    
    $state_names = array();
    if (is_array($states)) {
      foreach ($states as $state) {
        if (is_object($state)) {
          $state_names[] = $state->name;
        }
        else {
          $state_names[] = $state;
        }
      }
    }
    $state_names_list = db_placeholders($state_names, 'varchar');
    //-- We need the list of states twice in the query.
    $state_names = array_merge($state_names, $state_names);    
    $result = db_query("SELECT * FROM {workflow_transitions}
              WHERE state_name IN (" . $state_names_list . ")
              OR target_state_name IN (" . $state_names_list . ")", $state_names);
    
    $transitions = array();
    while ($transition = db_fetch_typed_object($result, "workflow_transition")) {
      $transition->roles = workflow::tab_roles_to_array($transition->roles);
      $transitions[] = $transition;
    }
    return $transitions;
  }
  
  /**
  * Query database for the node types associated with the current workflow 
  *
  * @param $workflow_name
  *   The machine name of the workflow
  *
  * @return
  *   The array of node_type names associated with the workflow from the database.  
  */
  static function query_nodetypes($workflow_name) {
    if (empty($workflow_name)) {
      return array();
    }

    //static $node_types;    
    if (!empty(self::$node_types_cache[$workflow_name])) {
      return self::$node_types_cache[$workflow_name];
    }
    
    $result = db_query("SELECT type FROM {workflow_type_map} WHERE workflow_name = '%s'", $workflow_name);
    
    $types = array();
    while ($row = db_fetch_object($result)) {
      $types[] = $row->type;
    }
    
    self::$node_types_cache[$workflow_name] = $types;
    return $types;
  }
  static $node_types_cache = array();
  
}

function workflow_init() {
  
  $schema = ctools_export_get_schema('workflows');
  $export = $schema['export'];
  
}

/************* Worfklow API Hook Implementations *******************/

function workflow_export_crud_load($name) {
   
  ctools_include('export');
  $table = "workflows";
  $schema = ctools_export_get_schema($table);
  $export = $schema['export'];

  $workflows = ctools_export_load_object($table, 'names', array($name));
  if (isset($workflows[$name])) {
    $workflow = $workflows[$name];
    //-- If object is served from the database (Normal or Overriden)
    //-- then we need to load data from all the joined tables.
    if ($workflow->export_type & EXPORT_IN_DATABASE) {
      $workflow->states = workflow::query_states($workflow->name);  
      $workflow->transitions = workflow::query_transitions($workflow->states);  
      $workflow->node_types = workflow::query_nodetypes($workflow->name);  
      $workflow->roles = workflow::tab_roles_to_array($workflow->tab_roles);        
    }
    $workflow->export_type = EXPORT_IN_CODE;
    return $workflow;
  }
}

/**
* Delete workflow object. You can pass the workflow's machine name instead of a full object.
*/
function workflow_export_crud_delete($object) {
  
  ctools_include('export');
  $table = 'workflows';
  
  $schema = ctools_export_get_schema($table);
  $export = $schema['export'];
  $workflow_name = is_object($object) ? $object->{$export['key']} : $object;
  
  /** Delete Workflow Transitions **/
  $result = db_query("SELECT name FROM {workflow_states} WHERE workflow_name = '%s'", $workflow_name);
    
  $state_names = array();
  while ($row = db_fetch_typed_object($result, 'workflow_state')) {
    $state_names[] = $row->name;
  }
    
  if (sizeof($state_names) > 0) {
    $state_names_list = db_placeholders($state_names, 'varchar');
    //-- We need the list of states twice in the query.
    $state_names = array_merge($state_names, $state_names);

    $result = db_query("DELETE FROM {workflow_transitions}
              WHERE state_name IN (" . $state_names_list . ")
              OR target_state_name IN (" . $state_names_list . ")", $state_names);    
  }
  /** End Delete Workflow Transitions **/
  
  db_query("DELETE FROM {workflows} WHERE " . $export['key'] . " = '%s'", $workflow_name);
  db_query("DELETE FROM {workflow_type_map} WHERE workflow_name = '%s'", $workflow_name);  
  db_query("DELETE FROM {workflow_states} WHERE workflow_name = '%s'", $workflow_name);    
  
  // Workflow deletion may affect tabs (local tasks), so force menu rebuild.
  cache_clear_all('*', 'cache_menu', TRUE);
  menu_rebuild();

}

function workflow_export_callback($workflow, $prefix = '') {
      
  ctools_include('export');
  if ($workflow->export_type & EXPORT_IN_DATABASE) {
    $workflow = workflow_export_crud_load($workflow->name);
  }
  
  $output = ctools_export_object('workflows', $workflow, $prefix);
  
  $output .= "$prefix" . '$workflow->states = array();' . "\n";  
  if (is_array($workflow->states)) {
    $counter = 0;
    foreach ($workflow->states as $state) {
      $output .= "\n";
      $output .= ctools_export_object('workflow_states', $state, $prefix);
      $output .= "$prefix" . '$workflow->states[' . $counter . '] = $workflow_state' . ";\n";
      $counter++;      
    }
    $output .= "\n";
  }

  $output .= "$prefix" . '$workflow->transitions = array();' . "\n";  
  if (is_array($workflow->transitions)) {
    $counter = 0;
    foreach ($workflow->transitions as $transition) {
      $output .= "\n";
      $output .= ctools_export_object('workflow_transitions', $transition, $prefix);
      $output .= "$prefix" . '$workflow->transitions[' . $counter . '] = $workflow_transition' . ";\n";
      $counter++;      
    }
    $output .= "\n";
  }

  
  $output .= "$prefix" . '$workflow->roles = ' . ctools_var_export($workflow->roles, $prefix) . ";\n";
  $output .= "$prefix" . '$workflow->node_types = ' . ctools_var_export($workflow->node_types, $prefix) . ";\n";  
  
  return $output;
  
}

/**
* Tinyints are interpreted as booleans by CTools. To avoid that we need custom exporter (or we'd have to change field type).
*/
function workflow_state_weight_exporter($workflow, $field, $value, $indent) {
  if ($field == 'weight') {
    return (int) $workflow->weight;
  }
}


/**
 * While db_fetch_object in Drupal is just a delegator to DB-specific fetch_object() functions, the original ones can
 * load a resultset in a class-typed object. Unfortunately, Drupal always puts stuff on stdClass. This function bypasses
 * the limitation.
 *
 * @see: http://drupal.org/node/950270 for the status of trying to fix this in D6 core.
 *
 * @param $resultset
 *   DB resultset resource
 *
 * @param $classname
 *   Class name to type-cast the DB row to.
 *
 * @return
 *   type-casted object
 */
function db_fetch_typed_object($resultset, $classname) {
  global $db_type;
  
  switch ($db_type) {
    case 'mysqli':
      $object = mysqli_fetch_object($resultset, $classname);
      return isset($object) ? $object : FALSE;
    case 'mysql':
      return mysql_fetch_object($resultset, $classname);
    case 'pgsql':
      return pg_fetch_object($resultset, NULL, $classname);    
    default:
      //-- Sensible default, but the object will be of stdClass type, so may not fully support logic in other places.
      return db_fetch_object($resultset);
  }
}
